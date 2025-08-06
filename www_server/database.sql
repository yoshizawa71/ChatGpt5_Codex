CREATE TABLE IF NOT EXISTS dispositivo (
    id INT AUTO_INCREMENT PRIMARY KEY,
    id_dispositivo VARCHAR(30) NOT NULL,
    escala DOUBLE NOT NULL,
    frequencia INT NOT NULL,
    finalizado_processo_fabrica TINYINT(1),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
)  ENGINE=INNODB;

CREATE TABLE IF NOT EXISTS config (
    c_id INT AUTO_INCREMENT PRIMARY KEY,
    apn VARCHAR(50) NOT NULL,
    apn_usuario VARCHAR(50) NOT NULL,
    apn_senha VARCHAR(50) NOT NULL,
    servidor_url VARCHAR(50) NOT NULL,
    servidor_porta INT NOT NULL,
    servidor_usuario VARCHAR(50),
    servidor_chave VARCHAR(50),
    servidor_config_url VARCHAR(50) NOT NULL,
    servidor_config_porta INT NOT NULL,
    versao_firmware VARCHAR(50) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    id_dispositivo INT,
    FOREIGN KEY(id_dispositivo) REFERENCES dispositivo(id)
)  ENGINE=INNODB;