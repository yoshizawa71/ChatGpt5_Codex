<?php
try{
    $data = json_decode(file_get_contents('php://input'), true);

    $pdo = new PDO("mysql:dbname=".$_SERVER['DB_DATALOGGER'].";host=".$_SERVER['DB_DATALOGGER_HOST'], $_SERVER['DB_USER_DATALOGGER'], $_SERVER['DB_PASSWORD_DATALOGGER']);
    
    $sql = "UPDATE config set versao_firmware=:versao_firmware where c_id > 0;";
    
    $statement = $pdo->prepare($sql);
    
    if ($data["versao_firmware"] != NULL) {
        $statement->bindParam(':versao_firmware', $data['versao_firmware']);
    }

    $statement->execute();
} catch (Exception $e) {
    echo 'Exceção capturada: ',  $e->getMessage(), "\n";
}
exit;
 ?>
