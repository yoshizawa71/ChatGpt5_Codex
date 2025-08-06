<?php
try{
    $data = json_decode(file_get_contents('php://input'), true);

    $pdo = new PDO("mysql:dbname=".$_SERVER['DB_DATALOGGER'].";host=".$_SERVER['DB_DATALOGGER_HOST'], $_SERVER['DB_USER_DATALOGGER'], $_SERVER['DB_PASSWORD_DATALOGGER']);
    
    $sql = "UPDATE dispositivo SET ";
    if ($data["id_dispositivo"] != NULL) {
        $sql.= "id_dispositivo=:id_dispositivo, ";
    }
    if ($data["escala"] != NULL) {
        $sql.= "escala=:escala, ";
    }
    if ($data["frequencia"] != NULL) {
        $sql.= "frequencia=:frequencia, ";
    }
    
    $sql.= "finalizado_processo_fabrica=:finalizado_processo_fabrica, ";
    
    $sql = substr($sql, 0, -2)." ";
    
    $sql .= "WHERE id=:id; ";
    
    $statement = $pdo->prepare($sql);
    
    if ($data["id_dispositivo"] != NULL)
        $statement->bindParam(':id_dispositivo', $data['id_dispositivo']);
    if ($data["escala"] != NULL)
        $statement->bindParam(':escala', $data['escala']);
    if ($data["frequencia"] != NULL)
        $statement->bindParam(':frequencia', $data['frequencia']);
    
    $statement->bindParam(':finalizado_processo_fabrica', $data['finalizado_processo_fabrica']);
        
    $statement->bindParam(':id', $data['id']);
    $statement->execute();
} catch (Exception $e) {
    echo 'Exceção capturada: ',  $e->getMessage(), "\n";
}
exit;
 ?>
