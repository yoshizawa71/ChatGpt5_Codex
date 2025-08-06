<?php
try{
    $pdo = new PDO("mysql:dbname=".$_SERVER['DB_DATALOGGER'].";host=".$_SERVER['DB_DATALOGGER_HOST'], $_SERVER['DB_USER_DATALOGGER'], $_SERVER['DB_PASSWORD_DATALOGGER']);
    $statement = $pdo->prepare("SELECT * FROM dispositivo as d INNER JOIN config as c ON c.id_dispositivo=d.id WHERE d.id=:disp_id");
    $statement->bindParam(':disp_id', $_GET['disp_id']);
    $statement->execute();
    $results = $statement->fetchAll(PDO::FETCH_ASSOC);
    echo json_encode($results);
} catch (Exception $e) {
    echo 'Exceção capturada: ',  $e->getMessage(), "\n";
}
exit;
 ?>