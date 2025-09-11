#Front-end Code
html_content='''
<!DOCTYPE html>
<html>
<head>
    <title>Autonomous Robot Control Dashboard</title>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <!-- Leaflet CSS -->
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.7.1/dist/leaflet.css" />
    <!-- Leaflet Draw CSS -->
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/leaflet.draw/1.0.4/leaflet.draw.css" />
    <!-- Leaflet Rotated Marker CSS -->
    <link rel="stylesheet" href="https://rawcdn.githack.com/bbecquet/Leaflet.RotatedMarker/master/leaflet.rotatedMarker.css" />
    <style>
        :root {
            --primary-color: #ecf0f1;
            --secondary-color: #34495e;
            --accent-color: #e67e22;
            --danger-color: #e74c3c;
            --success-color: #2ecc71;
            --text-color: #ecf0f1;
            --bg-color: #2c3e50;
            --button-padding: 8px 16px;
            --font-size: 14px;
            --control-gap: 10px;
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: "Segoe UI", Tahoma, Geneva, Verdana, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
        }

        .dashboard {
            display: grid;
            grid-template-columns: 1fr 1fr 1fr;
            grid-gap: 10px;
            padding: 10px;
            height: 100vh;
            width: 100vw;
        }

        .panel {
            background: var(--secondary-color);
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.3);
            padding: 10px;
            display: flex;
            flex-direction: column;
            overflow: hidden;
        }

        .control-panel {
            grid-column: span 1;
            display: flex;
            flex-direction: column;
            gap: 10px;
        }

        .maps-container {
            grid-column: span 2;
            display: grid;
            grid-template-rows: 1fr auto;
            gap: 10px;
        }

        .maps-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
        }

        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding-bottom: 10px;
            border-bottom: 1px solid var(--primary-color);
        }

        .title {
            font-size: 20px;
            font-weight: 600;
            color: var(--primary-color);
        }

        .status {
            padding: 4px 8px;
            border-radius: 15px;
            background: var(--success-color);
            color: white;
            font-size: 12px;
        }

        #video {
            width: 100%;
            height: 180px;
            border-radius: 6px;
            object-fit: cover;
            margin: 10px 0;
        }

        .controls-grid {
            display: grid;
            grid-template-columns: 1fr;
            gap: 10px;
        }

        .control-group {
            background: var(--bg-color);
            padding: 10px;
            border-radius: 6px;
            display: flex;
            flex-direction: column;
            gap: 8px;
            margin-top: 5px;
            border-style: groove;
            border-color: #c8cacb;
        }

        .button-group {
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }

        button {
            background: var(--accent-color);
            color: white;
            border: none;
            padding: var(--button-padding);
            border-radius: 4px;
            cursor: pointer;
            font-weight: 500;
            font-size: var(--font-size);
            transition: transform 0.1s, background 0.3s;
            flex: 1 1 30%;
            text-align: center;
        }

        button:hover {
            background: #d35400;
            transform: translateY(-1px);
        }

        button.danger {
            background: var(--danger-color);
        }

        button.danger:hover {
            background: #c0392b;
        }

        input[type="number"], input[type="text"] {
            width: 60px;
            padding: 6px;
            border: 1px solid #555;
            border-radius: 4px;
            margin: 0 5px;
            background-color: var(--secondary-color);
            color: var(--text-color);
            font-size: var(--font-size);
            text-align: center;
        }

        input[type="number"]::placeholder, input[type="text"]::placeholder {
            color: var(--text-color);
        }

        #map {
            height: 400px;
            border-radius: 6px;
            overflow: hidden;
        }

        .input-group {
            display: flex;
            align-items: center;
            justify-content: space-between;
        }

        .input-group label {
            flex: 1;
            color: var(--primary-color);
            font-size: var(--font-size);
        }

        .input-group input {
            flex: 1;
            max-width: 80px;
        }

        .update-pid {
            background: var(--accent-color);
            color: white;
            border: none;
            padding: 6px 12px;
            border-radius: 4px;
            cursor: pointer;
            font-weight: 500;
            font-size: var(--font-size);
            transition: background 0.3s;
            align-self: flex-end;
        }

        .update-pid:hover {
            background: #d35400;
        }

        .key-container{
            display: grid;
            grid-template-columns: auto auto auto;
            width: 100%;
            align-items: center;
            justify-content: center;
            text-align: center;

        }
        .key-container div{

            display: block;
            margin:5px;

            button{
                display: block;
                text-align: center;
                font-size: 20px;
                font-weight: bold;
                padding: 50px;
            }

            .stop{
                width: 50px;
                height: 50px;
                margin: 10px;
                display: flex;
                align-items: center;
                justify-content: center;
                text-align: center;
            }
        }

        @media (max-width: 1200px) {
            .dashboard {
                grid-template-columns: 1fr;
            }

            .maps-container {
                grid-column: span 1;
            }

            .maps-grid {
                grid-template-columns: 1fr;
            }

            #video {
                height: 150px;
            }

            .panel {
                height: auto;
            }

            #map {
                height: 300px;
            }
        }
    </style>
</head>
<body>
    <div class="dashboard">
        <div class="control-panel">
            <div class="panel">
                <div class="header">
                    <h1 class="title">Robot Control</h1>
                    <span class="status">Online</span>
                </div>
                <img id="video" src="/video_feed" alt="Robot Camera Feed">
                <div class="controls-grid">
                    <!-- Face Detection Settings -->
                    <div class="control-group">
                        <h3>Face Detection</h3>
                        <div class="input-group">
                            <label for="desiredFaceArea">Face Area:</label>
                            <input type="number" id="desiredFaceArea" value="5000" min="1000" max="10000">
                            <button onclick="increaseFaceArea()">+</button>
                            <button onclick="decreaseFaceArea()">-</button>
                        </div>
                        <div class="input-group">
                            <label for="centerOffset">Center Offset:</label>
                            <input type="number" id="centerOffset" value="0" min="-320" max="320">
                            <button onclick="moveCenterLeft()">&#8592;</button>
                            <button onclick="moveCenterRight()">&#8594;</button>
                        </div>
                    </div>
                    <!-- PID Controller Settings -->
                    <div class="control-group">
                        <h3>PID Controller</h3>
                        <div class="input-group">
                            <label for="kp">Kp:</label>
                            <input type="number" id="kp" value="0.5" step="0.01">
                        </div>
                        <div class="input-group">
                            <label for="ki">Ki:</label>
                            <input type="number" id="ki" value="0.0001" step="0.0001">
                        </div>
                        <div class="input-group">
                            <label for="kd">Kd:</label>
                            <input type="number" id="kd" value="0.25" step="0.01">
                        </div>
                        <button class="update-pid" onclick="updatePID()">Update PID</button>
                    </div>
                    <!-- Hose Rail Controls -->
                    <!-- This section wont be used for the 106 robot
                    <div class="control-group">
                        <h3>Drip Line Controls</h3>
                        <div class="button-group">
                            <button onclick="moveRailForward()">Forward</button>
                            <button onclick="moveRailBackward()">Backward</button>
                            <button onclick="stopRail()">Stop</button>
                        </div>
                    </div>
                    -->
                    <div class="control-group">
                        <h2>Current Value: <span id="csv-value">Loading...</span></h2>
                        <h3>Large Tank: Pump Controls</h3>
                        <div class="button-group">
                            <button onclick="pumpON()">ON</button>
                            <button onclick="pumpOFF()">OFF</button>
                        </div>
                    </div>
                </div>
            </div>
        </div>
        <div class="maps-container">
            <div class="maps-grid">
                <div class="panel">
                    <div class="header">
                        <h2 class="title">Location Tracking</h2>
                    </div>
                    <div id="map"></div>
                </div>
                <!-- Replaced Path Planning panel with Mode panel -->
                 
                <div class="panel">
                    <div class="header">
                        <h2 class="title">Mode Controls</h2>
                    </div>
                    <div class="control-group">                       
                        <div class="button-group">
                            <button onclick="setMode('basic_movement')">Basic Movement</button>
                            <button onclick="setMode('auto_navigation')">Auto-Navigation</button>
                            <button onclick="setMode('face_tracking')">Face Tracking</button>
                        </div>
                    </div>
                    <!-- Auto-Navigation Controls -->
                    <div class="control-group" id="autoNavControls" style="display: none;">
                        <h3>Auto-Navigation Controls</h3>
                        <div class="button-group">
                            <button id="sendButton">Send Command</button>
                            <button id="estopButton" class="danger">E-Stop</button>
                            <button id="undoEstopButton">Undo E-Stop</button>
                        </div>
                    </div>
                    <!-- Robot Movement Controls -->
                    <div class="control-group">
                        <h3>Movement Controls</h3>
                        <div class="button-group">
                        <div class="key-container">
                            <div></div>
                            <div><button onclick="moveForward()">⬆︎</button></div>
                            <div></div>
                            <div><button onclick="moveLeft()">⬅︎</button></div>
                            <div><button class="stop" onclick="stopRobot()"> STOP </button></div>
                            <div><button onclick="moveRight()">➡︎</button></div>
                            <div></div>
                            <div><button onclick="moveBackward()">⬇︎</button></div>
                            <div></div>
                        </div>
                        </div>
                    </div>
                </div>
            </div>
            <div class="panel">
                <div class="controls-grid">
                    <!-- Emergency Controls -->
                    <div class="control-group">
                        <h3>Emergency Controls</h3>
                        <div class="button-group">
                            <button class="danger" onclick="sendEStop()">E-Stop</button>
                            <button onclick="undoEStop()">Resume</button>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <!-- Scripts -->
    <!-- Leaflet JS -->
    <script src="https://unpkg.com/leaflet@1.7.1/dist/leaflet.js"></script>
    <!-- Leaflet Draw JS -->
    <script src="https://cdnjs.cloudflare.com/ajax/libs/leaflet.draw/1.0.4/leaflet.draw.js"></script>
    <!-- Leaflet Rotated Marker JS -->
    <script src="https://rawcdn.githack.com/bbecquet/Leaflet.RotatedMarker/master/leaflet.rotatedMarker.js"></script>
    <script>
        var map;
        var robotMarker;
        var robotIcon;
        var drawnItems;
        var drawControl;
        var pathPolyline = null;
        var plannedPath = null;
        var gpsData = [];

    async function fetchCSVValue() {
      try {
        const response = await fetch('moisture_data.csv'); // URL to your CSV
        const data = await response.text();
        const rows = data.trim().split('\n');
        const latestRow = rows[1]; // skip header
        const value = latestRow.split(',')[1];
        document.getElementById('csv-value').textContent = value;
      } catch (error) {
        document.getElementById('csv-value').textContent = 'Error';
        console.error('CSV fetch error:', error);
      }
    }

    fetchCSVValue();

        // Emergency Controls
        function sendEStop() {
            fetch('/estop', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                alert(data.status);
            })
            .catch(error => console.error('Error:', error));
        }

        function undoEStop() {
            fetch('/undo_estop', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                alert(data.status);
            })
            .catch(error => console.error('Error:', error));
        }
        
        document.addEventListener("keydown", function(event) {
            if (event.key === "w") {moveForward();}
            else if(event.key === "a") {moveLeft();}
            else if(event.key === "s") {moveBackward();}
            else if(event.key === "d") {moveRight();}
            else if(event.key === " ") {stopRobot();}
        });    
            
        // Robot Movement Controls
        function moveForward() {
            fetch('/move_forward', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Robot moving forward');
            })
            .catch(error => console.error('Error:', error));
        }
        
        function moveRailForward() {
            fetch('/move_rail_forward', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Rail going forward');
            })
            .catch(error => console.error('Error:', error));
        }

        function pumpON() {
            fetch('/pump_on', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Turning Pump ON');
            })
            .catch(error => console.error('Error:', error));
        }
        
        function pumpOFF() {
            fetch('/pump_off', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Turning Pump ON');
            })
            .catch(error => console.error('Error:', error));
        }

        function moveBackward() {
            fetch('/move_backward', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Robot moving backward');
            })
            .catch(error => console.error('Error:', error));
        }
        
        function moveRailBackward() {
            fetch('/move_rail_backward', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Rail going backward');
            })
            .catch(error => console.error('Error:', error));
        }

        function moveLeft() {
            fetch('/move_left', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Robot moving left');
            })
            .catch(error => console.error('Error:', error));
        }

        function moveRight() {
            fetch('/move_right', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Robot moving right');
            })
            .catch(error => console.error('Error:', error));
        }

        function stopRobot() {
            fetch('/stop_robot', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Robot stopped');
            })
            .catch(error => console.error('Error:', error));
        }
        
        function stopRail() {
            fetch('/stop_rail', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                console.log('Rail stopped');
            })
            .catch(error => console.error('Error:', error));
        }

        // Face Detection Settings
        function increaseFaceArea() {
            fetch('/increase_face_area', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                document.getElementById('desiredFaceArea').value = data.desired_face_area;
            })
            .catch(error => console.error('Error:', error));
        }

        function decreaseFaceArea() {
            fetch('/decrease_face_area', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                document.getElementById('desiredFaceArea').value = data.desired_face_area;
            })
            .catch(error => console.error('Error:', error));
        }

        function moveCenterLeft() {
            fetch('/move_center_left', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                document.getElementById('centerOffset').value = data.center_offset;
            })
            .catch(error => console.error('Error:', error));
        }

        function moveCenterRight() {
            fetch('/move_center_right', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                document.getElementById('centerOffset').value = data.center_offset;
            })
            .catch(error => console.error('Error:', error));
        }

        // PID Controller Settings
        function updatePID() {
            var kp = parseFloat(document.getElementById('kp').value);
            var ki = parseFloat(document.getElementById('ki').value);
            var kd = parseFloat(document.getElementById('kd').value);

            var data = { kp: kp, ki: ki, kd: kd };

            fetch('/update_pid', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            })
            .then(response => response.json())
            .then(data => {
                alert('PID parameters updated');
            })
            .catch(error => console.error('Error:', error));
        }

        // Mode Selection
        function setMode(mode) {
            fetch('/set_mode', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ mode: mode })
            })
            .then(response => response.json())
            .then(data => {
                alert('Mode set to ' + mode.replace('_', ' '));
                if (mode === 'auto_navigation') {
                    document.getElementById('autoNavControls').style.display = 'block';
                    initializeDrawing();
                } else {
                    document.getElementById('autoNavControls').style.display = 'none';
                    removeDrawing();
                }
            })
            .catch(error => console.error('Error:', error));
        }

        function initializeDrawing() {
            drawnItems = new L.FeatureGroup();
            map.addLayer(drawnItems);

            drawControl = new L.Control.Draw({
                edit: {
                    featureGroup: drawnItems
                }
            });
            map.addControl(drawControl);

            map.on(L.Draw.Event.CREATED, function (event) {
                var layer = event.layer;
                drawnItems.addLayer(layer);
            });

            document.getElementById('sendButton').onclick = function() {
                var coordinates = [];
                drawnItems.eachLayer(function(layer) {
                    if (layer instanceof L.Polyline || layer instanceof L.Polygon) {
                        var latLngs = layer.getLatLngs();
                        if (latLngs.length > 0 && Array.isArray(latLngs[0])) {
                            latLngs.forEach(function(latlngArray) {
                                latlngArray.forEach(function(latlng) {
                                    coordinates.push({ lat: latlng.lat, lng: latlng.lng });
                                });
                            });
                        } else {
                            latLngs.forEach(function(latlng) {
                                coordinates.push({ lat: latlng.lat, lng: latlng.lng });
                            });
                        }
                    } else if (layer instanceof L.Marker) {
                        var latlng = layer.getLatLng();
                        coordinates.push({ lat: latlng.lat, lng: latlng.lng });
                    }
                });

                fetch('/send_coordinates', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify({ coordinates: coordinates })
                })
                .then(response => response.json())
                .then(data => {
                    console.log(data);
                    alert("Coordinates sent to the server");
                    if (plannedPath) {
                        map.removeLayer(plannedPath);
                    }
                    plannedPath = L.polyline(coordinates.map(c => [c.lat, c.lng]), {color: 'green'}).addTo(map);
                })
                .catch(error => console.error('Error:', error));
            };

            document.getElementById('estopButton').onclick = function() {
                sendEStop();
            };

            document.getElementById('undoEstopButton').onclick = function() {
                undoEStop();
            };
        }

        function removeDrawing() {
            if (drawControl) {
                map.removeControl(drawControl);
                drawControl = null;
            }
            if (drawnItems) {
                map.removeLayer(drawnItems);
                drawnItems = null;
            }
            if (plannedPath) {
                map.removeLayer(plannedPath);
                plannedPath = null;
            }
            if (pathPolyline) {
                map.removeLayer(pathPolyline);
                pathPolyline = null;
            }
        }

        // Initialize the map
        map = L.map('map').setView([0, 0], 2);

        // Add OpenStreetMap tile layer
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '&copy; OpenStreetMap contributors'
        }).addTo(map);

        // Robot marker with rotation
        robotIcon = L.icon({
            iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-red.png',
            iconSize: [25, 41],
            iconAnchor: [12, 41],
        });

        robotMarker = L.marker([0, 0], {
            icon: robotIcon,
            rotationAngle: 0
        }).addTo(map);

        // Fetch initial GPS position
        fetch('/initial_gps')
            .then(response => response.json())
            .then(data => {
                var initialLat = data.lat || 0;
                var initialLon = data.lon || 0;
                map.setView([initialLat, initialLon], 18);
                robotMarker.setLatLng([initialLat, initialLon]);
            });

        // Update robot position and heading periodically
        setInterval(function() {
            fetch('/get_gps_data')
                .then(response => response.json())
                .then(data => {
                    if (data && data.length > 0) {
                        gpsData = data;
                        var gpsCoordinates = data.map(function(point) {
                            return [point.Estimated_Lat || point.GPS_Lat, point.Estimated_Lon || point.GPS_Lon];
                        });

                        var latestPosition = gpsCoordinates[gpsCoordinates.length - 1];
                        robotMarker.setLatLng(latestPosition);

                        var heading = data[data.length - 1].Estimated_Theta || data[data.length - 1].Heading || 0;
                        robotMarker.setRotationAngle(heading);

                        if (pathPolyline) {
                            pathPolyline.setLatLngs(gpsCoordinates);
                        } else {
                            pathPolyline = L.polyline(gpsCoordinates, {color: 'blue'}).addTo(map);
                        }
                    }
                })
                .catch(error => console.error('Error fetching GPS data:', error));
        }, 1000);
    </script>
</body>
</html>
'''
