<?php
try{
    $data = json_decode(file_get_contents('php://input'), true);

    $pdo = new PDO("mysql:dbname=".$_SERVER['DB_DATALOGGER'].";host=".$_SERVER['DB_DATALOGGER_HOST'], $_SERVER['DB_USER_DATALOGGER'], $_SERVER['DB_PASSWORD_DATALOGGER']);
    
    $sql = "INSERT INTO dispositivo(id_dispositivo, escala, frequencia, finalizado_processo_fabrica) values (:id_dispositivo, :escala, :frequencia, :finalizado_processo_fabrica);";
    
    
    $statement = $pdo->prepare($sql);
    
    if ($data["id_dispositivo"] != NULL) {
        $statement->bindParam(':id_dispositivo', $data['id_dispositivo']);
        $escala = 0.002;
        $statement->bindParam(':escala', $escala);
        $frequencia = 300;
        $statement->bindParam(':frequencia', $frequencia);
        $fabrica = 1;
        $statement->bindParam(':finalizado_processo_fabrica', $fabrica);
        
        
    }

    if($statement->execute()) {
        $sql = "INSERT INTO config(apn, apn_usuario, apn_senha, servidor_url, servidor_porta, servidor_usuario, servidor_chave, servidor_config_url, servidor_config_porta, versao_firmware, id_dispositivo) VALUES (:apn, :apn_usuario, :apn_senha, :servidor_url, :servidor_porta, :servidor_usuario, :servidor_chave, :servidor_config_url, :servidor_config_porta, :versao_firmware, LAST_INSERT_ID());";
        $statement = $pdo->prepare($sql);
        
        $apn="virtueyes.vivo.com.br";
        $statement->bindParam(':apn', $apn);
        $apn_usuario = "vivo";
        $statement->bindParam(':apn_usuario', $apn_usuario);
        $apn_senha = "vivo";
        $statement->bindParam(':apn_senha', $apn_senha);
        $servidor_url = "http://Teste";
        $statement->bindParam(':servidor_url', $servidor_url);
        $servidor_porta = 80;
        $statement->bindParam(':servidor_porta', $servidor_porta);
        $servidor_usuario = "teste";
        $statement->bindParam(':servidor_usuario', $servidor_usuario);
        $servidor_chave = "teste";
        $statement->bindParam(':servidor_chave', $servidor_chave);
        $servidor_config_url = "http://Teste";
        $statement->bindParam(':servidor_config_url', $servidor_config_url);
        $servidor_config_porta = 80;
        $statement->bindParam(':servidor_config_porta', $servidor_config_porta);
        $versao_firmware = "1.0.0";
        $statement->bindParam(':versao_firmware', $versao_firmware);
        
        $statement->execute();
    }
} catch (Exception $e) {
    echo 'Exceção capturada: ',  $e->getMessage(), "\n";
}
exit;
 ?>
