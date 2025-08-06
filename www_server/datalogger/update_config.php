<?php
try{
    $data = json_decode(file_get_contents('php://input'), true);

    $pdo = new PDO("mysql:dbname=".$_SERVER['DB_DATALOGGER'].";host=".$_SERVER['DB_DATALOGGER_HOST'], $_SERVER['DB_USER_DATALOGGER'], $_SERVER['DB_PASSWORD_DATALOGGER']);
    
    $sql = "UPDATE config SET ";
    if ($data["apn"] != NULL) {
        $sql.= "apn=:apn, ";
    }
    if ($data["apn_usuario"] != NULL) {
        $sql.= "apn_usuario=:apn_usuario, ";
    }
    if ($data["apn_senha"] != NULL) {
        $sql.= "apn_senha=:apn_senha, ";
    }
    if ($data["servidor_url"] != NULL) {
        $sql.= "servidor_url=:servidor_url, ";
    }
    if ($data["servidor_porta"] != NULL) {
        $sql.= "servidor_porta=:servidor_porta, ";
    }
    if ($data["servidor_usuario"] != NULL) {
        $sql.= "servidor_usuario=:servidor_usuario, ";
    }
    if ($data["servidor_chave"] != NULL) {
        $sql.= "servidor_chave=:servidor_chave, ";
    }
    if ($data["servidor_config_url"] != NULL) {
        $sql.= "servidor_config_url=:servidor_config_url, ";
    }
    if ($data["servidor_config_porta"] != NULL) {
        $sql.= "servidor_config_porta=:servidor_config_porta, ";
    }
    if ($data["versao_firmware"] != NULL) {
        $sql.= "versao_firmware=:versao_firmware, ";
    }
    
    $sql = substr($sql, 0, -2)." ";
    
    $sql .= "WHERE c_id=:c_id; ";
    
    $statement = $pdo->prepare($sql);
    
    if ($data["apn"] != NULL)
        $statement->bindParam(':apn', $data['apn']);
    if ($data["apn_usuario"] != NULL)
        $statement->bindParam(':apn_usuario', $data['apn_usuario']);
    if ($data["apn_senha"] != NULL)
        $statement->bindParam(':apn_senha', $data['apn_senha']);
    if ($data["servidor_url"] != NULL)
        $statement->bindParam(':servidor_url', $data['servidor_url']);
    if ($data["servidor_porta"] != NULL)
        $statement->bindParam(':servidor_porta', $data['servidor_porta']);
    if ($data["servidor_usuario"] != NULL)
        $statement->bindParam(':servidor_usuario', $data['servidor_usuario']);
    if ($data["servidor_chave"] != NULL)
        $statement->bindParam(':servidor_chave', $data['servidor_chave']);
    if ($data["servidor_config_url"] != NULL)
        $statement->bindParam(':servidor_config_url', $data['servidor_config_url']);
    if ($data["servidor_config_porta"] != NULL)
        $statement->bindParam(':servidor_config_porta', $data['servidor_config_porta']);
    if ($data["versao_firmware"] != NULL)
        $statement->bindParam(':versao_firmware', $data['versao_firmware']);
        
    $statement->bindParam(':c_id', $data['c_id']);
    $statement->execute();
} catch (Exception $e) {
    echo 'Exceção capturada: ',  $e->getMessage(), "\n";
}
exit;
 ?>
