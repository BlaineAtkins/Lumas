#When flashing a new batch of Lumas, run this program on your computer while you flash each ESP with FirmwareInstaller.ino to record their MAC addresses 
SERVER_IP='10.0.0.211' #IP of the computer this is running on
filename='MACs.csv'

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
        
        message = "received"
        self.wfile.write(bytes(message, "utf8"))

        with open(filename) as myfile:
            if post_data.decode("utf-8") in myfile.read():
                print('--ALREADY LOGGED, SKIPPING')
            else:
                print('--Saving to file')
                with open(filename,"a") as myfile:
                    myfile.write("\n")
                    myfile.write(post_data.decode("utf-8"))
                    myfile.write(",")
                    myfile.write("None")
                    myfile.write(",")
                    myfile.write("PLACEHOLDER IP")
                    myfile.write(",")
                    myfile.write("whatever else")

with HTTPServer((SERVER_IP, 8000), handler) as server:
    server.serve_forever()








