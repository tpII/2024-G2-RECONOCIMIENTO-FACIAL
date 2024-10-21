from flask import Flask, request, jsonify
from paho.mqtt.client import Client
import cv2
import numpy as np
from imgbeddings import imgbeddings
from PIL import Image
import psycopg2
import os
import base64

app = Flask(__name__)
IMAGE_FOLDER = './uploads'
if not os.path.exists(IMAGE_FOLDER):
    os.makedirs(IMAGE_FOLDER)

# Cargo el archivo del algoritmo haarcascade en la variable alg
alg = "/content/haarcascade_frontalface_default.xml"
# Lo paso a OpenCV
haar_cascade = cv2.CascadeClassifier(alg)

@app.route('/add_face', methods=['POST'])
def add_face():
    # Cargo la imagen en la variable image_file
    image_file = request.files['image']
    # Leo la imagen
    img = cv2.imread(image_file, 0)
    # Creo una version en blanco y negro de la imagen
    gray_img = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
    # Detecto las caras
    faces = haar_cascade.detectMultiScale(
        gray_img, scaleFactor=1.05, minNeighbors=5, minSize=(50, 50)
    )
    # Imprimo el numero de caras detectadas
    print(len(faces))

    i = 0
    # Por cada cara detectada
    for x, y, w, h in faces:
        # crop the image to select only the face
        cropped_image = img[y: y + h, x: x + w]
        # loading the target image path into target_file_name variable  - replace <INSERT YOUR TARGET IMAGE NAME HERE> with the path to your target image
        target_file_name = 'temp-faces/' + str(i) + '.jpg'
        cv2.imwrite(
            target_file_name,
            cropped_image,
        )
        i = i + 1

    # Variable de entorno para ocultar url
    db_url = os.getenv("DATABASE_URL")
    # Conexion con la base de datos
    conn = psycopg2.connect(db_url)

    for filename in os.listdir("temp-faces"):
        # Abro la imagen
        img = Image.open("temp-faces/" + filename)
        # Cargo los `imgbeddings`
        ibed = imgbeddings()
        # Calculo los embeddings
        embedding = ibed.to_embeddings(img)
        cur = conn.cursor()
        cur.execute("INSERT INTO pictures values (%s,%s)", (filename, embedding[0].tolist()))
        print(filename)
    conn.commit()

def on_connect(client, userdata, flags, rc):
    print(f"Conectado al broker MQTT con código {rc}")
    client.subscribe("home/security/camera")

def on_message(client, userdata, msg):
    # Decodifico el mensaje en base64
    image_data = msg.payload.decode('utf-8')  # Decodificamos el payload de bytes a string

    # Verifico si el mensaje contiene un prefijo de Base64 'data:image/jpeg;base64,'
    if image_data.startswith('data:image/jpeg;base64,'):
        image_data = image_data.replace('data:image/jpeg;base64,', '')

    # Decodifico la cadena Base64
    image_bytes = base64.b64decode(image_data)

    # Guardo la imagen en un archivo
    image_path = os.path.join(IMAGE_FOLDER, 'received_photo.jpg')
    with open(image_path, 'wb') as f:
        f.write(image_bytes)

    print(f'Imagen guardada en: {image_path}')
    # ----------------------------------------------------------
    # Comienzo de procesamiento y deteccion de cara
    # Abro la imagen
    img = Image.open(image_path)
    # Cargo los `imgbeddings`
    ibed = imgbeddings()
    # Calculo los embeddings
    embedding = ibed.to_embeddings(img)

    # Conexion con la base de datos
    db_url = os.getenv("DATABASE_URL")
    conn = psycopg2.connect(db_url)

    cur = conn.cursor()
    string_representation = "[" + ",".join(str(x) for x in embedding[0].tolist()) + "]"
    cur.execute(
        """
        SELECT embedding <=> %s AS distance
        FROM pictures
        ORDER BY distance
        LIMIT 1;
        """,
        (string_representation,)
    )
    row = cur.fetchone()

    if row:
        distancia = row[0]  # Distancia devuelta desde la query

        THRESHOLD = 0.17
        if distancia <= THRESHOLD:
            print(f'Conocida (Distancia: {distancia})')
        else:
            print(f'Desconocida (Distancia: {distancia})')
    else:
        print('No se encontró ninguna coincidencia.')
    cur.close()

client = Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("127.0.0.1", 1884, 60)
client.loop_forever()

@app.route('/')
def hello_world():  # put application's code here
    return 'Hello World!'


if __name__ == '__main__':
    app.run()
