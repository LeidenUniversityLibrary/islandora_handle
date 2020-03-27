<?php

/**
 * Base class for deriving Handle implementations.
 */
abstract class IslandoraHandleHandleHandler {

  protected $prefix;
  protected $targetUrl;
  protected $pid;
  protected $authorizationHeader;
  private $mdhandles = array();

  /**
   * IslandoraHandleHandleHandler constructor.
   *
   * @param AbstractObject|null $object
   *   An AbstractObject if doing things on an object context, NULL if just a
   *   Handle handler is needed.
   * @param string|null $prefix
   *   The prefix being used for this Handle or NULL if defaulting to the one
   *   defined in the admin form.
   */
  public function __construct(AbstractObject $object = NULL, $prefix = NULL) {
    if ($object instanceof AbstractObject) {
      $this->targetUrl = $this->constructTargetUrl($object);
      $this->pid = $object->id;
    }
    if (is_null($prefix)) {
      $prefix = variable_get('islandora_handle_server_prefix', '1234567');
    }
    $this->prefix = $prefix;
    $this->authorizationHeader = format_string('Basic !auth', array(
      '!auth' => base64_encode(variable_get('islandora_handle_server_admin_username', 'handleAdmin') . ':' . variable_get('islandora_handle_server_admin_password', 'superSecretPassword')),
    ));
  }

  /**
   * Construct the suffix for the Handle URL.
   *
   * @param AbstractObject $object
   *   An AbstractObject representing an object within Fedora.
   *
   * @return string
   *   The full Handle Service URL containing the Handle.
   */
  public function constructSuffix(AbstractObject $object) {
    // For now we are just specifying that the suffix is the PID of the object.
    // In the future we can modify this as we see fit.
    return $object->id;
  }

  /**
   * Helper to construct a target URL.
   *
   * Constructed from islandora/object, appended with the $pid of an object and
   * prepended with either the local server's URL or the custom configured
   * variable islandora_handle_host.
   *
   * @param string|AbstractObject $object
   *   The pid of the object or object itself that is the target of the URL.
   *
   * @return string
   *   The constructed target URL.
   */
  public function constructTargetUrl($object) {
    $pid = $object instanceof AbstractObject ? $object->id : $object;
    if (!$this->targetUrl) {
      // $hostvar will be populated if host resolver is different than ingestion
      // service.
      $hostvar = variable_get('islandora_handle_host', '');
      // We call the function url with language such that we don't get language
      // specific prefixes in the URL.
      // Also, if alias = TRUE it means that the path is used as-is, no alias
      // for the path is used.
      $target_url = url("islandora/object/$pid", array(
        'language' => (object) array('language' => FALSE),
        'absolute' => empty($hostvar),
        'alias'    => !variable_get('islandora_handle_use_alias', FALSE),
      ));
      if (empty($hostvar)) {
        $this->targetUrl = $target_url;
      }
      else {
        $this->targetUrl = rtrim($hostvar, '/') . '/' . ltrim($target_url, '/');
      }
    }
    return $this->targetUrl;
  }

  /**
   * Helper to return a full Handle URL.
   *
   * @param string|AbstractObject $handle
   *   A Handle that we are updating in prefix/suffix form or an AbstractObject
   *   that is being updated. In the AbstractObject case a Handle URL will be
   *   constructed or retrieved from metadata to compare .
   *
   * @return string
   *   A handle containing the prefix and suffix.
   */
  public function getFullHandle($handle) {
    if ($handle instanceof AbstractObject) {
      if (variable_get('islandora_handle_check_metadata', FALSE)) {
        $mdhandle = $this->getHandleFromMetadata($handle);
        if ($mdhandle) {
          return $mdhandle;
        }
      }
      return format_string('!prefix/!suffix', array(
        '!prefix' => $this->prefix,
        '!suffix' => $this->constructSuffix($handle),
      ));
    }
    else {
      return $handle;
    }
  }

  /**
   * Helper to return the Handle.net URL for the metadata.
   *
   * @param AbstractObject $object
   *   The object having a value appended to its metadata.
   *
   * @return string
   *   The Handle.net URL to be used in the metadata.
   */
  public function getHandleMetadataValue(AbstractObject $object) {
    return format_string('http://hdl.handle.net/!handle', array(
      '!handle' => $this->getFullHandle($object),
    ));
  }

  /**
   * Adds our Handle value to the specified datastream.
   *
   * @param AbstractObject $object
   *   An AbstractObject representing an object within Fedora.
   * @param string $datastream
   *   The datastream to which we are applying the Handle value to.
   * @param string $xsl_location
   *   The uri location of the XSL we are applying to append the Handle.
   *
   * @return array
   *   An array describing the outcome of the operation.
   */
  public function appendHandleToMetadata(AbstractObject $object, $datastream, $xsl_location) {
    $xslt = new XSLTProcessor();
    $xsl_transform = new DOMDocument();
    $ds_content = $object[$datastream]->content;
    $xsl_transform->load($xsl_location);
    $input = new DOMDocument();
    $did_load_ok = $input->loadXML($ds_content);
    if ($did_load_ok) {
      $xslt->importStylesheet($xsl_transform);
      $xslt->setParameter('', 'handle_value', $this->getHandleMetadataValue($object));
      $ds_output = $xslt->transformToDoc($input);
      if ($ds_output) {
        // If there is nothing to change don't make a new version.
        $xml_content = $ds_output->saveXML();
        if (!islandora_handle_datastream_content_equal($input, $ds_output)) {
          $object[$datastream]->content = $xml_content;
          return array(
            'success' => TRUE,
            'message' => array(
              'message' => t('Appended Handle to @ds datastream for @pid!'),
              'message_sub' => array('@pid' => $object->id, '@ds' => $datastream),
              'type' => 'dsm',
            ),
          );
        }
      }
      else {
        return array(
          'success' => FALSE,
          'message' => array(
            'message' => t('Appending the Handle value for @obj on the @ds datastream failed!'),
            'message_sub' => array('@obj' => $object->id, '@ds' => $datastream),
            'type' => 'watchdog',
            'severity' => WATCHDOG_ERROR,
          ),
        );
      }
    }
    else {
      return array(
        'success' => FALSE,
        'message' => array(
          'message' => t('Unable to load content of the @ds datastream for @obj!'),
          'message_sub' => array('@obj' => $object->id, '@ds' => $datastream),
          'type' => 'watchdog',
          'severity' => WATCHDOG_ERROR,
        ),
      );
    }
  }

  /**
   * Helper: Returns handle that was found in the metadata.
   *
   * @param AbstractObject $object
   *   An AbstractObject representing an object in Fedora.
   *
   * @return string/bool
   *   The handle if it exists in the metadata; FALSE if it does not exist.
   */
  public function getHandleFromMetadata(AbstractObject $object, $dstocheck = NULL) {
    if (!is_array($dstocheck)) {
      $dstocheck = array('DC', 'MODS');
    }
    if (isset($this->mdhandles[$object->id])) {
      return $this->mdhandles[$object->id];
    }
    $handle = FALSE;
    if (in_array('DC', $dstocheck) && isset($object['DC'])) {
      $dom = new DomDocument();
      $dom->loadXML($object['DC']->content);
      $dom_xpath = new DomXPath($dom);
      $dom_xpath->registerNamespace('oai_dc', 'http://www.openarchives.org/OAI/2.0/oai_dc/');
      $dom_xpath->registerNamespace('dc', 'http://purl.org/dc/elements/1.1/');
      $handleneturl = "http://hdl.handle.net/";
      $identifier_results = $dom_xpath->query('/oai_dc:dc/dc:identifier[starts-with(text(), "' . $handleneturl . '")]');
      foreach ($identifier_results as $node) {
        if (strlen($node->nodeValue) > strlen($handleneturl)) {
          $handle = substr($node->nodeValue, strlen($handleneturl));
        }
      }
    }
    if (!$handle && in_array('MODS', $dstocheck) && isset($object['MODS'])) {
      $dom = new DomDocument();
      $dom->loadXML($object['MODS']->content);
      $dom_xpath = new DomXPath($dom);
      $dom_xpath->registerNamespace('mods', 'http://www.loc.gov/mods/v3');
      $handleneturl = "http://hdl.handle.net/";
      $identifier_results = $dom_xpath->query('/mods:mods/mods:identifier[starts-with(text(), "' . $handleneturl . '") or @type="hdl"]');
      foreach ($identifier_results as $node) {
        if (strlen($node->nodeValue) > strlen($handleneturl)) {
          $handle = substr($node->nodeValue, strlen($handleneturl));
        }
      }
    }
    if ($handle) {
      // Check that handle has the right format, possibly prefixed with hdl:.
      $matches = array();
      if (preg_match("!^(?:hdl:|info:hdl/)?([^/]+/[^/]+)$!", $handle, $matches) === 1) {
        $handle = $matches[1];
      }
      // Check that the handle has the right prefix.
      list($prefix, $suffix) = explode('/', $handle);
      $prefixes = variable_get('islandora_handle_alternative_server_prefixes', array());
      if ($prefix !== $this->prefix && !in_array($prefix, $prefixes)) {
        watchdog('islandora_handle', 'Found handle "@handle" in metadata with unexpected prefix "@prefix".', array(
          '@handle' => $handle,
          '@prefix' => $prefix,
        ), WATCHDOG_ERROR);
        $handle = FALSE;
      }
    }
    $this->mdhandles[$object->id] = $handle;

    return $handle;
  }

  /**
   * Helper; actually mint the Handle.
   *
   * @param AbstractObject $object
   *   The AbstractObject having a handle minted for it.
   *
   * @return bool
   *   TRUE if successful; FALSE otherwise.
   */
  abstract public function createHandle(AbstractObject $object);

  /**
   * Helper; get the Handle location.
   *
   * @param string|AbstractObject $handle
   *   A Handle that we are reading in prefix/suffix form or an AbstractObject
   *   that is being updated. In the AbstractObject case a Handle URL will be
   *   constructed to compare.
   *
   * @return string|bool
   *   The location of the Handle if it exists; FALSE otherwise.
   */
  abstract public function readHandle($handle);

  /**
   * Helper; update the Handle location.
   *
   * @param string|AbstractObject $handle
   *   A Handle that we are updating in prefix/suffix form or an AbstractObject
   *   that is being updated. In the AbstractObject case a Handle URL will be
   *   constructed to compare.
   * @param string $target
   *   The target URL the Handle is being updated to point to.
   *
   * @return bool
   *   TRUE if successful; FALSE otherwise.
   */
  abstract public function updateHandle($handle, $target);

  /**
   * Helper; delete the Handle.
   *
   * @param string|AbstractObject $handle
   *   A Handle that we are deleting in prefix/suffix form or an AbstractObject
   *   that is being deleted. In the AbstractObject case a Handle URL will be
   *   constructed to compare.
   *
   * @return bool
   *   TRUE if successful; FALSE otherwise.
   */
  abstract public function deleteHandle($handle);

}
