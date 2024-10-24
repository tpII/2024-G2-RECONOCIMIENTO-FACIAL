# Sistema de Seguridad Doméstica con Reconocimiento Facial 🤖📷

Este proyecto consiste en un sistema de seguridad para el hogar que utiliza un ESP32 con cámara para 
monitorear y detectar movimiento. El sistema envía alertas a través de una aplicación web y almacena imágenes en una base de 
datos. Utiliza una API de inteligencia artificial para reconocimiento facial, emitiendo alarmas en caso de detectar personas desconocidas.

## Arduino

Para la correcta ejecucion del codigo de arduino, debe crearse además de los archivos de la carpeta, uno llamado
**arduino_secrets.h**, que contenga las credenciales necesarias. Dentro del mismo hay que declarar:
```C
#define NETWORK_ID "Nombre de la red wifi"
#define NETWORK_PASS "Contraseña de la red wifi"
#define MY_IP "IP local de la computadora"
```

## MQTT

Debe instalarse mosquitto. Dentro de la ruta del mismo (en general suele ser ```C:\Program Files\mosquitto```) abrir un cmd
como administrador para poder cambiar el archivo **mosquitto.conf**. Dentro del mismo habrá que agregar:
* allow_anonymous true
* listener 1884

Una vez guardado el archivo con esa configuracion y parados en la misma ruta, vamos a iniciar al broker con el siguiente comando:
```
mosquitto -c “C:\Program Files\mosquitto\mosquitto.conf” -v
```

## Flask

Se deben instalar todas las dependencias del archivo requirements.txt
Lo ideal es tener la **version 3.12** de python para el interpreter para que no ocurran errores.

Para correr el servidor de flask se debe usar el siguiente comando, luego de haber iniciado el broker de MQTT.

```python
flask run --host=0.0.0.0
```

Debe tenerse en cuenta la creacion del archivo **.env** en la carpeta raiz del proyecto de flask, que contenga 
la URI de la base de datos dentro de una variable **DATABASE_URL**