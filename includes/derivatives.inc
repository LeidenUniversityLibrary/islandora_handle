<?php

/**
 * @file
 * Constructs derivatives for the Islandora Handle module.
 */

/**
 * Appends a Handle from specified configuration.
 *
 * @param AbstractObject $object
 *   An AbstractObject representing an object within Fedora.
 * @param bool $force
 *   Whether we are forcing the derivs or not.
 * @param array $hook
 *   An array describing the hook.
 *
 * @return array
 *   An array describing the success of the operation.
 */
function islandora_handle_append_handle_from_configuration(AbstractObject $object, $force = FALSE, array $hook = array()) {
  module_load_include('inc', 'islandora_handle', 'includes/handle');

  $success = TRUE;
  $messages = array();
  $handler_class = islandora_handle_retrieve_selected_handler();
  $handler = new $handler_class($object);
  $handle_updated = FALSE;

  $target = $handler->readHandle($object);
  if (!$target || $force) {
    if (!$target) {
      if ($handler->createHandle($object)) {
        $messages[] = array(
          'message' => t('Created Handle for @pid!'),
          'message_sub' => array('@pid' => $object->id),
          'type' => 'dsm',
        );
      }
      else {
        return array(
          'success' => FALSE,
          'messages' => array(
            array(
              'message' => t('Error constructing Handle for @obj!.'),
              'message_sub' => array('@obj' => $object->id),
              'type' => 'watchdog',
              'severity' => WATCHDOG_ERROR,
            ),
          ),
        );
      }
    }
    else {
      $newtarget = $handler->constructTargetUrl($object);
      if ($target !== $newtarget) {
        if ($handler->updateHandle($object, $newtarget)) {
          $handle_updated = TRUE;
          $messages[] = array(
            'message' => t('Update Handle for @pid!'),
            'message_sub' => array('@pid' => $object->id),
            'type' => 'dsm',
          );
        }
        else {
          return array(
            'success' => FALSE,
            'messages' => array(
              array(
                'message' => t('Error updating Handle for @obj!.'),
                'message_sub' => array('@obj' => $object->id),
                'type' => 'watchdog',
                'severity' => WATCHDOG_ERROR,
              ),
            ),
          );
        }
      }
    }

    $metadata_updated = FALSE;
    foreach (islandora_handle_retrieve_configurations_by_cmodels($object->models) as $assoc) {
      if ($hook['destination_dsid'] == $assoc->datastream && isset($object[$assoc->datastream])) {
        $add_outcome = $handler->appendHandleToMetadata($object, $assoc->datastream, $assoc->xsl_location);
        // XXX: When there's nothing to be done in the appending of metadata
        // (when the datastream already contains the handle value) don't report
        // any derivative logging as nothing actually happened.
        if (isset($add_outcome['success'])) {
          $metadata_updated = TRUE;
          if (!$add_outcome['success']) {
            $success = FALSE;
          }
          $messages[] = $add_outcome['message'];
        }
        break;
      }
    }

    // In the event nothing gets updated is no messages/success result to pass
    // on to the derivative logging.
    if ($metadata_updated || $handle_updated) {
      return array(
        'success' => $success,
        'messages' => $messages,
      );
    }
  }
  return array();
}

/**
 * Appends the Handle value to the DC Identifier field of an object.
 *
 * @param AbstractObject $object
 *   An AbstractObject representing an object in Fedora.
 * @param bool $force
 *   Whether the derivatives are being forced or not.
 * @param array $hook
 *   An array describing the hook.
 *
 * @return array
 *   An array describing the outcome of the derivative process.
 */
function islandora_handle_add_to_dc_datastream(AbstractObject $object, $force = FALSE, array $hook = array()) {
  module_load_include('inc', 'islandora_handle', 'includes/handle');
  $handler_class = islandora_handle_retrieve_selected_handler();
  $handler = new $handler_class();
  $handle_existence = $handler->readHandle($object);
  $dc_existence = isset($object['DC']);
  if ($dc_existence && $handle_existence) {
    // This could trigger many times so let's be sure that the current Handle
    // value doesn't already exist to not create multiple versions.
    $dom = new DomDocument();
    $dom->preserveWhiteSpace = FALSE;
    $dom->formatOutput = TRUE;
    $dom->loadXML($object['DC']->content);
    $dom_xpath = new DomXPath($dom);
    $dom_xpath->registerNamespace('oai_dc', 'http://www.openarchives.org/OAI/2.0/oai_dc/');
    $dom_xpath->registerNamespace('dc', 'http://purl.org/dc/elements/1.1/');

    $update = TRUE;
    $handle_replaced = FALSE;
    $handle_url = $handler->getHandleMetadataValue($object);
    $handle_node = $dom->createElementNS('http://purl.org/dc/elements/1.1/', 'dc:identifier', $handle_url);

    // First check if one exists such that we can replace it.
    $protocol = variable_get('islandora_handle_use_https', FALSE) ? 'https' : 'http';
    $query = "/oai_dc:dc/dc:identifier[starts-with(text(), \"{$protocol}://hdl.handle.net\")]";
    $identifier_results = $dom_xpath->query($query);
    foreach ($identifier_results as $node) {
      // This could trigger many times so let's be sure that the current Handle
      // value doesn't already exist to not create multiple versions.
      if ($node->nodeValue != $handle_url) {
        // Delete an existing Handle identifier if it exists and append the new
        // one.
        $parent = $node->parentNode;
        $parent->insertBefore($handle_node, $node);
        $parent->removeChild($node);
        // Should only be one result so let's break just incase.
        $handle_replaced = TRUE;
        break;
      }
      else {
        $update = FALSE;
      }
    }
    if (!$handle_replaced) {
      $dom->documentElement->appendChild($handle_node);
    }

    if ($update) {
      $object['DC']->content = $dom->saveXML();
      return array(
        'success' => TRUE,
        'messages' => array(
          array(
            'message' => t('Updated the Handle in the Dublin Core for @pid.'),
            'message_sub' => array('@pid' => $object->id),
            'type' => 'dsm',
          ),
        ),
      );
    }
  }
  else {
    $messages = array();
    if (!$handle_existence) {
      $messages[] = array(
        'message' => t('Unable to append Handle to DC as the Handle was not constructed for @obj!'),
        'message_sub' => array('@obj' => $object->id),
        'type' => 'watchdog',
        'severity' => WATCHDOG_ERROR,
      );
    }
    if (!$dc_existence) {
      $messages[] = array(
        'message' => t('Unable to append Handle to DC as the datastream does not exist for @obj!'),
        'message_sub' => array('@obj' => $object->id),
        'type' => 'watchdog',
        'severity' => WATCHDOG_ERROR,
      );
      watchdog('islandora_handle', "Unable to append Handle to DC as the datastream does not exist for @obj!", array('@obj' => $object->id), WATCHDOG_ERROR);
    }
    return array(
      'success' => FALSE,
      'messages' => $messages,
    );
  }
}

/**
 * Delete handle if datastreams do not exist on object.
 *
 * @param AbstractObject $object
 *   An AbstractObject representing an object in Fedora.
 * @param bool $force
 *   Whether the derivatives are being forced or not.
 * @param array $hook
 *   An array describing the hook.
 *
 * @return array
 *   An array describing the outcome of the derivative process.
 */
function islandora_handle_clean_up_handle(AbstractObject $object, $force = FALSE, array $hook = array()) {
  // Edge case that could occur if for some reason the datastream(s)
  // were deleted after the handle was created. No use having a
  // Handle present when it's not appended anywhere...
  module_load_include('inc', 'islandora_handle', 'includes/handle');
  $messages = array();
  $handler_class = islandora_handle_retrieve_selected_handler();
  $handler = new $handler_class($object);

  if ($handler->readHandle($object)) {
    $found = FALSE;
    $associations = islandora_handle_retrieve_configurations_by_cmodels($object->models);
    foreach ($associations as $assoc) {
      if (isset($object[$assoc->datastream])) {
        $found = TRUE;
        break;
      }
    }

    if ($found == FALSE) {
      // Clean up DC.
      $dom = new DomDocument();
      $dom->preserveWhiteSpace = FALSE;
      $dom->formatOutput = TRUE;
      $dom->loadXML($object['DC']->content);
      $dom_xpath = new DomXPath($dom);
      $dom_xpath->registerNamespace('oai_dc', 'http://www.openarchives.org/OAI/2.0/oai_dc/');
      $dom_xpath->registerNamespace('dc', 'http://purl.org/dc/elements/1.1/');

      $update = FALSE;
      $handle_url = $handler->getHandleMetadataValue($object);
      $identifier_results = $dom_xpath->query('/oai_dc:dc/dc:identifier[text() = "' . $handle_url . '"]');
      foreach ($identifier_results as $node) {
        $node->parentNode->removeChild($node);
        $update = TRUE;
      }

      if ($update) {
        $object['DC']->content = $dom->saveXML();
      }

      if (!$handler->deleteHandle($object->id)) {
        $messages[] = array(
          'message' => t('Unable to delete Handle.'),
          'type' => 'watchdog',
          'severity' => WATCHDOG_ERROR,
        );
        return array(
          'success' => FALSE,
          'messages' => $messages,
        );
      }
    }
  }
  return array(
    'success' => TRUE,
    'messages' => array(),
  );
}
