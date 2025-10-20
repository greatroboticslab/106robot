import paho.mqtt.client as mqtt
import json
import csv
from datetime import datetime
import os

filename = 'moisture_data.csv'
file_exists = os.path.isfile(filename)

csv_file = open(filename, mode='a', newline='')
writer = csv.writer(csv_file)
if not file_exists:
    writer.writerow(['Timestamp', 'Mac Address', 'Data'])
    
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with code", rc)
    client.subscribe("moisture/data")
    
def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        mac = payload.get("mac")
        value = payload.get("value")
        
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"Received | {timestamp}, {mac}, {value}")
        writer.writerow([timestamp, mac, value])
        csv_file.flush()
        
    except Exception as e:
        print("Error processing message:", e)
        
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("localhost", 1883, 60)

client.loop_forever()   