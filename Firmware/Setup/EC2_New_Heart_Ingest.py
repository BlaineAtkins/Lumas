#This program should run on the EC2
#It takes NewHeartBatch created by LumasMacLogger.py (run locally in conjunction with FirmwareInstaller.ino), and..
#1: Adds rows to AWS DynamoDB
#2: Updates Mosquitto ACL with their usernames & passwords

import os
import boto3

#-----for dynamoDB-----
dynamodb = boto3.client('dynamodb', region_name='us-west-2')  # Replace with your region
TABLE_NAME = 'Hearts'


#-----below are functions for dynamoDB update
def chunked(iterable, size):
    """Yield successive chunks of specified size from iterable."""
    for i in range(0, len(iterable), size):
        yield iterable[i:i + size]

def format_item(item):
    """Convert a Python dict to DynamoDB item format."""
    return {k: {'S': str(v)} for k, v in item.items()}

def batch_write_devices(devices):
    for batch in chunked(devices, 25):
        request_items = {
            TABLE_NAME: [
                {'PutRequest': {'Item': format_item(device)}}
                for device in batch
            ]
        }

        response = dynamodb.batch_write_item(RequestItems=request_items)

        # Handle unprocessed items if any
        while response.get('UnprocessedItems'):
            print("Retrying unprocessed items...")
            response = dynamodb.batch_write_item(RequestItems=response['UnprocessedItems'])
#-----above are functions for dynamoDB update

#-----for dynamoDB-----
    
csv_file = 'NewHeartBatch.csv'
mosquittoPWFile='/etc/mosquitto/LumasPWFile'
devices = []

with open(csv_file, 'r') as file:
    lines = file.readlines()

# Extract headers from the first line
headers = lines[0].strip().split(',')
# Process each subsequent line
for line in lines[1:]:
    values = line.strip().split(',')
    device = dict(zip(headers, values))
    devices.append(device)

#print("Will now add rows to AWS DynamoDB")
# Rows to add to DynamoDB:
#for device in devices:
#    #print(device)
#    print("jk")
#print("for MQTT passwords, put USELOCAL for dynamodb")


print("Will now add entries to the Mosquitto Password file at "+mosquittoPWFile+"...")
#Things to add to Mosquitto ACL:
for device in devices:
    #print(str(device['id'].replace(":","-"))+" - "+str(device['mqtt_password']))

    os.system(str("sudo mosquitto_passwd -b "+mosquittoPWFile+" "+str(device['id'].replace(":","-"))+" "+str(device['mqtt_password'])))


#don't store the password in the DB, just an instruction for clients to use their EEPROM password
for device in devices:
    device["mqtt_password"] = "USELOCAL"

print("Will now add rows to AWS DynamoDB...")
batch_write_devices(devices)
print("done!")

#print (devices)




