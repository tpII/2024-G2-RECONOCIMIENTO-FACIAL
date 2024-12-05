from flask import Flask, request, jsonify, render_template
from paho.mqtt.client import Client
import cv2
import numpy as np
from imgbeddings import imgbeddings
from PIL import Image
import psycopg2
import os
import base64
from dotenv import load_dotenv
import time

app = Flask(__name__)

load_dotenv()

IMAGE_FOLDER = './uploads'
IMAGE_ADD_FACE_FOLDER = './uploaded-faces'
if not os.path.exists(IMAGE_FOLDER):
    os.makedirs(IMAGE_FOLDER)
if not os.path.exists(IMAGE_ADD_FACE_FOLDER):
    os.makedirs(IMAGE_ADD_FACE_FOLDER)

# Cargo el archivo del algoritmo haarcascade en la variable alg
alg = "haarcascade_frontalface_default.xml"
# Lo paso a OpenCV
haar_cascade = cv2.CascadeClassifier(alg)

# Funcion para agregar una nueva cara a la base de datos
@app.route('/add_face', methods=['POST'])
def add_face():
    # Me fijo si se selecciono una imagen
    if 'image' not in request.files:
        return jsonify({"error": "No se seleccionó ninguna imagen."}), 400

    # Cargo la imagen en la variable image_file
    image_file = request.files['image']
    if image_file.filename == '':
        return jsonify({"error": "Archivo vacío."}), 400

    image_path = os.path.join(IMAGE_ADD_FACE_FOLDER, image_file.filename)
    image_file.save(image_path)

    # Leo la imagen
    img = cv2.imread(image_path, 0)
    # Creo una version en blanco y negro de la imagen
    gray_img = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
    # Detecto las caras
    faces = haar_cascade.detectMultiScale(
        gray_img, scaleFactor=1.05, minNeighbors=5, minSize=(75, 75)
    )
    # Imprimo el numero de caras detectadas
    print(len(faces))

    # Por cada cara detectada
    for x, y, w, h in faces:
        # crop the image to select only the face
        cropped_image = img[y: y + h, x: x + w]
        cv2.imwrite(
            'temp-faces/' + image_file.filename,
            cropped_image,
        )

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

    return jsonify({"message": f"{image_file.filename} agregada correctamente como cara conocida."})

def on_connect(client, userdata, flags, rc):
    print(f"Conectado al broker MQTT con código {rc}")
    client.subscribe("home/security/camera")

def on_message(client, userdata, msg):
    # Decodifico el mensaje en base64
    try:
        image_data = msg.payload.decode('utf-8')  # Decodificamos el payload de bytes a string
    except UnicodeDecodeError as e:
        print(f"Error al decodificar mensaje UTF-8: {e}")
        return

    # Verifico si el mensaje contiene un prefijo de Base64 'data:image/jpeg;base64,'
    if image_data.startswith('data:image/jpeg;base64,'):
        image_data = image_data.replace('data:image/jpeg;base64,', '')

    # Decodifico la cadena Base64
    image_bytes = base64.b64decode(image_data)

    # Guardo la imagen en un archivo
    image_path = os.path.join(IMAGE_FOLDER, 'received_photo.jpeg')

    # Verificar tiempo de vida del archivo
    #if os.path.exists(image_path):
    #    last_modified = os.path.getmtime(image_path)
    #    if time.time() - last_modified > 30:  # Si el archivo tiene más de 30 segundos, omitir
    #        print("El archivo tiene más de 30 segundos, omitiendo procesamiento...")
    #        return

    with open(image_path, 'wb') as f:
        f.write(image_bytes)

    print(f'Imagen guardada en: {image_path}')

    # -------- TEST DE RECORTE DE IMAGEN PARA MEJORAR INFERENCIA ------
    # Leo la imagen
    img = cv2.imread(image_path, 0)
    # Creo una version en blanco y negro de la imagen
    gray_img = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
    # Detecto las caras
    faces = haar_cascade.detectMultiScale(
        gray_img, scaleFactor=1.05, minNeighbors=5, minSize=(75, 75)
    )
    # Imprimo el numero de caras detectadas
    print(len(faces))

    if(len(faces) >= 1):

        # Por cada cara detectada
        for x, y, w, h in faces:
            # crop the image to select only the face
            cropped_image = img[y: y + h, x: x + w]
            cv2.imwrite(
                "uploads/received_photo_face.jpeg",
                cropped_image,
            )

        # ----------------------------------------------------------
        # Comienzo de procesamiento y deteccion de cara
        # Abro la imagen
        img = Image.open("uploads/received_photo_face.jpeg")
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
                # Guardar foto desconocida en la base de datos
                with open("uploads/received_photo_face.jpeg", "rb") as img_file:
                    encoded_image = base64.b64encode(img_file.read()).decode('utf-8')
                    image_data = f"data:image/jpeg;base64,{encoded_image}"

                cur.execute("""
                        INSERT INTO pictures_log (picture)
                        VALUES (%s)
                    """, (image_data,))
                conn.commit()
        cur.close()
    else:
        print("No se detectaron caras")


@app.route('/notificaciones')
def notificaciones():
    db_url = os.getenv("DATABASE_URL")
    conn = psycopg2.connect(db_url)
    cur = conn.cursor()

    # Obtener fotos desconocidas
    cur.execute("""
        SELECT picture, timestamp
        FROM pictures_log
        ORDER BY timestamp DESC
    """)
    notifications = cur.fetchall()

    cur.execute("""
            UPDATE pictures_log
            SET leido = TRUE
            WHERE leido = FALSE
        """)
    conn.commit()

    # Borrar despues de 7 dias (opcional)
    #cur.execute("DELETE FROM pictures_log")
    #conn.commit()/

    cur.close()
    conn.close()
    return render_template('notificaciones.html', notifications=notifications)

client = Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("127.0.0.1", 1884, 60)
client.loop_start()

@app.route('/')
def index():  # put application's code here
    db_url = os.getenv("DATABASE_URL")
    conn = psycopg2.connect(db_url)
    cur = conn.cursor()

    # Contar notificaciones no vistas
    cur.execute("""
        SELECT COUNT(*)
        FROM pictures_log
        WHERE leido = FALSE
    """)
    unseen_count = cur.fetchone()[0]

    cur.close()
    conn.close()
    return render_template('index.html', unseen_count=unseen_count)


if __name__ == '__main__':
    app.run()
