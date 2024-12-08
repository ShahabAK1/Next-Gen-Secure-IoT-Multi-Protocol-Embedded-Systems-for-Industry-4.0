from datetime import datetime
from flask import Flask, request, jsonify
import pyodbc

app = Flask(__name__)

# Database connection details
server = 'DESKTOP-1FINHBA\\MSSQLSERVER01'  # Replace with your server instance name
database = 'master'  # Replace with your actual database name
username = 'DESKTOP-1FINHBA\\Shahab A.K'  # Windows Authentication username

# Function to connect to SQL Server using Windows Authentication
def get_db_connection():
    try:
        conn = pyodbc.connect(f'DRIVER={{ODBC Driver 17 for SQL Server}};SERVER={server};DATABASE={database};Trusted_Connection=yes')
        return conn
    except pyodbc.Error as e:
        print(f"Database connection error: {e}")
        raise e

@app.route('/insertData', methods=['POST'])
def insert_data():
    data = request.get_json()  # Get JSON data from the request
    cursor = None
    conn = None
    
    try:
        # Validate and extract expected fields from the received JSON payload
        internal_temperature = data.get('InternalTemperature')
        environmental_temperature = data.get('EnvironmentalTemperature', 0.0)  # Default if not provided
        pir_status = data.get('PIR_Status', 0)  # Default to 0 if missing
        accel_x = data.get('Accel_X')
        accel_y = data.get('Accel_Y')
        accel_z = data.get('Accel_Z')
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')  # Use current timestamp if not provided

        # Validate mandatory fields
        if internal_temperature is None or accel_x is None or accel_y is None or accel_z is None:
            return jsonify({'error': 'Missing required data fields'}), 400

        # Parse PIR status as boolean/int
        pir_status = 1 if str(pir_status).lower() in ['motion', '1', 'true'] else 0

        # Print data to be inserted
        print(f"Inserting Data: \nInternalTemperature: {internal_temperature}\n"
              f"EnvironmentalTemperature: {environmental_temperature}\nPIR_Status: {pir_status}\n"
              f"Accel_X: {accel_x}\nAccel_Y: {accel_y}\nAccel_Z: {accel_z}\n"
              f"Timestamp: {timestamp}")

        # Connect to the database
        conn = get_db_connection()
        cursor = conn.cursor()

        # Insert data into the table, excluding the ID column
        cursor.execute(""" 
            INSERT INTO EnvironmentalMonitoring (InternalTemperature, EnvironmentalTemperature, PIR_Status, Accel_X, Accel_Y, Accel_Z, Timestamp)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        """, (internal_temperature, environmental_temperature, pir_status, accel_x, accel_y, accel_z, timestamp))
        conn.commit()

        # Return success response
        return jsonify({'message': 'Data inserted successfully!'}), 200

    except Exception as e:
        print(f"Error: {e}")
        return jsonify({'error': str(e)}), 500
    finally:
        # Close the cursor and connection if they were initialized
        if cursor:
            cursor.close()
        if conn:
            conn.close()

if __name__ == '__main__':
    # Start Flask app on all interfaces, port 5000
    app.run(host='0.0.0.0', port=5000)
