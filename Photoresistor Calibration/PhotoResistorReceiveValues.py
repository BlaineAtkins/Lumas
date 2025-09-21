SERVER_IP='10.0.0.105' #IP of the computer this is running on
filename='RawDataPhotoResistor.csv'

lastColor=-1
fullLine= [0]*129

print("Will attempt to start server on "+SERVER_IP+" and save all received payloads to "+filename)

from http.server import BaseHTTPRequestHandler, HTTPServer

class handler(BaseHTTPRequestHandler):
    def do_POST(self):
        self.send_response(200)
        self.send_header('Content-type','text/html')
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length)
        self.end_headers()
        print("Received "+post_data.decode("utf-8"))
        
##        message = "received"
##        self.wfile.write(bytes(message, "utf8"))
##
##                with open(filename,"a") as myfile:
##                    myfile.write("\n")
##                    myfile.write(post_data.decode("utf-8"))
##                    myfile.write(",")
##                    myfile.write("None")
##                    myfile.write(",")
##                    myfile.write("PLACEHOLDER IP")
##                    myfile.write(",")
##                    myfile.write("whatever else")

        
        values = post_data.decode("utf-8").split(',')
        color=values[0]
        brightness=values[1]
        reading=values[2]

        global lastColor #tell python to look for this variable in global scope, not local
        global fullLine

        lineToWrite=str("")

        if(lastColor!=color): #if this is the start of a new color
            if(lastColor!=-1): #write previous line, but only if it's not the first pass
                with open("output.csv", "a") as file:
                    for value in fullLine:
                        file.write(str(value)+",")
                    file.write("\n")
            fullLine=[0]*129
            fullLine[0]=str(color)

        fullLine[int(int(brightness)/2+1)]=reading

        lastColor=color
        

with HTTPServer((SERVER_IP, 8000), handler) as server:
    server.serve_forever()







