<?php
/**
 * Created by PhpStorm.
 * User: david
 * Date: 17/12/14
 * Time: 20:31
 */

class SparqlUpdateIndexerService extends SearchApiAbstractService {

  public function __construct(SearchApiServer $server) {
    parent::__construct($server);
  }

  public function configurationForm(array $form, array &$form_state) {
    $form['sparql_update_uri'] = array(
      '#type' => 'textfield',
      '#title' => t('SPARQL Update URI'),
      '#description' => t('Enter the URI of the SPARQL Update service.'),
      '#default_value' => $this->options['sparql_update_uri'],
      '#required' => TRUE,
    );
    return $form;
  }

  public function configurationFormValidate(array $form, array &$values, array &$form_state) {

    if (!filter_var($values['sparql_update_uri'], FILTER_VALIDATE_URL)) {
      form_error($form['sparql_update_uri'], 'Please enter a valid URL.');
    }

  }

  /**
   * Notifies this server that it is about to be deleted from the database and
   * should therefore clean up, if appropriate.
   *
   * Note that you shouldn't call the server's save() method, or any
   * methods that might do that, from inside of this method as the server isn't
   * present in the database anymore at this point.
   *
   * By default, deletes all indexes from this server.
   */
  public function preDelete() {
    // Only react on real deletes, not on reverts.
    // @see https://drupal.org/node/1414078
    // This method could be removed once the above issue is fixed.
    if ($this->server->hasStatus(ENTITY_IN_CODE)) {
      return;
    }
    $indexes = search_api_index_load_multiple(FALSE, array('server' => $this->server->machine_name));
    foreach ($indexes as $index) {
      $this->removeIndex($index);
    }
  }

  /**
   * Add a new index to this server.
   *
   * @param SearchApiIndex $index
   *   The index to add.
   */
  public function addIndex(SearchApiIndex $index) {
    if (module_exists('search_api_multi') && module_exists('search_api_views')) {
      views_invalidate_cache();
    }
  }

  /**
   * Notify the server that the indexed field settings for the index have
   * changed.
   * If any user action is necessary as a result of this, the method should
   * use drupal_set_message() to notify the user.
   *
   * @param SearchApiIndex $index
   *   The updated index.
   *
   * @return
   *   TRUE, if this change affected the server in any way that forces it to
   *   re-index the content. FALSE otherwise.
   */
  public function fieldsUpdated(SearchApiIndex $index) {
    if (module_exists('search_api_multi') && module_exists('search_api_views')) {
      views_invalidate_cache();
    }
    return TRUE;
  }

  /**
   * Indexes the specified items.
   *
   * @param SearchApiIndex $index
   *   The search index for which items should be indexed.
   * @param array $items
   *   An array of items to be indexed, keyed by their id. The values are
   *   associative arrays of the fields to be stored, where each field is an
   *   array with the following keys:
   *   - type: One of the data types recognized by the Search API, or the
   *     special type "tokens" for fulltext fields.
   *   - original_type: The original type of the property, as defined by the
   *     datasource controller for the index's item type.
   *   - value: The value to index.
   *
   *   The special field "search_api_language" contains the item's language and
   *   should always be indexed.
   *
   *   The value of fields with the "tokens" type is an array of tokens. Each
   *   token is an array containing the following keys:
   *   - value: The word that the token represents.
   *   - score: A score for the importance of that word.
   *
   * @return array
   *   An array of the ids of all items that were successfully indexed.
   *
   * @throws SearchApiException
   *   If indexing was prevented by a fundamental configuration error.
   */
  public function indexItems(SearchApiIndex $index, array $items) {
    foreach ($items as $id => $item) {
      try {
        $rdf_model = rdfx_get_rdf_model($index->item_type, $id);
        // do something with $rdf->uri and $rdf->index.
        // The save method should throw an exception on failure.
        if (easyrdf()) {

          $resource = $rdf_model->uri;
          $client = new EasyRdf_Sparql_Client($this->options['sparql_update_uri']);

          foreach ($rdf_model->index as $graph_uri => $graph_data) {
            $graph = new EasyRdf_Graph($graph_uri);
            foreach ($graph_data as $property => $values) {
              foreach ($values as $value) {
                $graph->add($graph_uri, $property, $value);
              }
            }
            $client->insert($graph, $resource . '.rdf');
          }

        }
      } catch (Exception $e) {
        watchdog_exception('sparql_update_indexer', $e);
      }
    }
    return array_keys($items);
  }

  /**
   * Deletes indexed items from this server.
   *
   * Might be either used to delete some items (given by their ids) from a
   * specified index, or all items from that index, or all items from all
   * indexes on this server.
   *
   * @param $ids
   *   Either an array containing the ids of the items that should be deleted,
   *   or 'all' if all items should be deleted. Other formats might be
   *   recognized by implementing classes, but these are not standardized.
   * @param SearchApiIndex $index
   *   The index from which items should be deleted, or NULL if all indexes on
   *   this server should be cleared (then, $ids has to be 'all').
   *
   * @throws SearchApiException
   *   If an error occurred while trying to delete the items.
   */
  public function deleteItems($ids = 'all', SearchApiIndex $index = NULL) {
    if (easyrdf()) {
      try {
        if ($ids = 'all') {
          // delete all items.
          $client = new EasyRdf_Sparql_Client(
            $this->options['sparql_query_uri'],
            $this->options['sparql_update_uri']
          );
          $client->clear('ALL');
        }
        elseif (is_array($ids)) {
          $store = new EasyRdf_GraphStore($this->options['update_uri']);
          foreach ($ids as $id) {
            $rdf_model = rdfx_get_rdf_model($index->item_type, $id);
            // delete one item.
            $resource = $rdf_model->uri;
            $store->delete($resource);
          }
        }
      } catch (Exception $e) {
        watchdog_exception('sparql_update_indexer', $e);
      }
    }

  }

  /**
   * Executes a search on the server represented by this object.
   *
   * @param $query
   *   The SearchApiQueryInterface object to execute.
   *
   * @return array
   *   An associative array containing the search results, as required by
   *   SearchApiQueryInterface::execute().
   *
   * @throws SearchApiException
   *   If an error prevented the search from completing.
   */
  public function search(SearchApiQueryInterface $query) {
    throw new SearchApiException(t('The Sparql Update Indexer service does not support search. Please query the SPARQL endpoint directly if the RDF store provides such a service.'));
  }
}