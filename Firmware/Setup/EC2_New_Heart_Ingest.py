#This program should run on the EC2
#It takes NewHeartBatch created by LumasMacLogger.py (run locally in conjunction with FirmwareInstaller.ino), and..
#1: Adds rows to AWS DynamoDB
#2: Updates Mosquitto ACL with their usernames & passwords

import os

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

print("Will now add rows to AWS DynamoDB")
# Rows to add to DynamoDB:
for device in devices:
    #print(device)
    print("jk")


print("Will now add entries to the Mosquitto Password file at "+mosquittoPWFile)
#Things to add to Mosquitto ACL:
for device in devices:
    print(str(device['id'].replace(":",""))+" - "+str(device['mqtt_password']))

    os.system(str("sudo mosquitto_passwd -b "+mosquittoPWFile+" "+str(device['id'].replace(":",""))+" "+str(device['mqtt_password'])))
