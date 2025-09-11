# ----------------------------------
# ------- central_script.py --------
# ----------------------------------

# ----------- Libraries ------------
import cv2
import numpy as np
import paho.mqtt.client as mqtt
import threading
import time
import json
import math
import sys
import base64
import gpsd
import csv
import logging
from multiprocessing import Process, Queue, Event
from flask import Flask, Response, jsonify, request, render_template_string

# -------- Internal Modules --------
import IMU
import GUI
from face_tracking import face_tracking_process
from auto_navigation import auto_navigation_process

# ----------------------------------
# ------- MQTT Configuration -------
MQTT_SERVER = "192.168.1.145"  # Update if different
MQTT_PORT   = 1883

# ------------ Channels ------------
MQTT_TOPIC_COMMAND      = "robot/control"
MQTT_RAIL_TOPIC_COMMAND = "robot/rail"
MQTT_TOPIC_DETECTIONS   = "robot/detections"
MQTT_TOPIC_CAMERA       = "robot/camera"
MQTT_TOPIC_PUMP         = "robot/pump"
MQTT_TOPIC_IMU          = "imu/data"
MQTT_TOPIC_REMOTE_PUMP  = "robot/remotepump"
MQTT_TOPIC_DATA         = "moisture/data"
# ----------------------------------

# ----------- Constants ------------
# PID CONTROLLER PARAMETES
FRAME_WIDTH         = 640
FRAME_HEIGHT        = 480
FRAME_CENTER_X      = FRAME_WIDTH // 2
# CONFIGURATION FLAGS
ENABLE_FRAME_FLIP   = True  # False: Disable frame flipping
INVERT_YAW_CONTROL  = False # True: if robot moves opposing direction
# ----------------------------------

# ------------ Globals -------------
latest_detection    = None
latest_camera_frame = None
output_frame        = None
gps_data            = []
current_lat         = None  # GPS latitude
current_lon         = None  # GPS longitude
robot_heading       = 0.0   # Heading data
e_stop_active       = False # E-Stop state
moisture_threshold  = 100   # For pump control with moisture

# MOVEMENTS MODE CONTROLS - DEFAULT
current_mode        = 'BASIC_MOV'

# LOCKS
lock                = threading.Lock()
gps_data_lock       = threading.Lock()

# QUEUES for inter-process communication
command_queue       = Queue()
detection_queue     = Queue()
camera_frame_queue  = Queue()
gps_data_queue      = Queue()
imu_queue           = Queue()

# EVENT to control processes
stop_event          = Event()

# Flask App
app = Flask(__name__)
logging.basicConfig(level=logging.INFO)
# ----------------------------------

# MQTT setup and functions
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"Connected to MQTT server {MQTT_SERVER} successfully.")
        #SUCRIBE TO TOPICS
        client.subscribe([(MQTT_TOPIC_DETECTIONS, 0), 
                          (MQTT_TOPIC_CAMERA, 0), 
                          (MQTT_TOPIC_DATA, 0)])
    else:
        print(f"Failed to connect to MQTT server, return code {rc}")