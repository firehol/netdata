import sys                                                                                                              
import ssl                                                                                                              
import paho.mqtt.client as mqtt

def on_connect(mqttc, obj, flags, rc):                                                                                  
    print("connected rc: "+str(rc))
def on_disconnect(mqttc, obj, flags, rc):                                                                                  
    print("disconnected rc: "+str(rc))                                                                                            
def on_message(mqttc, obj, msg):                                                                                        
    print(msg.topic+" "+str(msg.qos)+" "+str(msg.payload))                                                              
def on_publish(mqttc, obj, mid):                                                                                        
    print("mid: "+str(mid))                                                                                             
def on_subscribe(mqttc, obj, mid, granted_qos):                                                                         
    print("Subscribed: "+str(mid)+" "+str(granted_qos))                                                                 
def on_log(mqttc, obj, level, string):                                                                                  
    print(string)
print("Starting paho-inspection", flush=True)                                                                                                 
mqttc = mqtt.Client(transport='websockets')                                                                             
#mqttc.tls_set(certfile="server.crt", keyfile="server.key", cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS, ciphers=None)
#mqttc.tls_set(ca_certs="server.crt", cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS, ciphers=None)           
mqttc.tls_set(cert_reqs=ssl.CERT_NONE, tls_version=ssl.PROTOCOL_TLS, ciphers=None)           
mqttc.tls_insecure_set(True)                                                                                            
mqttc.on_message = on_message                                                                                           
mqttc.on_connect = on_connect                                                                                           
mqttc.on_disconnect = on_disconnect                                                                                           
mqttc.on_publish = on_publish                                                                                           
mqttc.on_subscribe = on_subscribe                                                                                       
mqttc.connect("vernemq", 9002, 60)                                                                                
mqttc.subscribe("/agent/#",0)                                                                                           
#mqttc.publish("/agent/mine","Test1")                                                                                    
#mqttc.subscribe("$SYS/#", 0) 
print("Connected succesfully, monitoring /agent/#", flush=True)                                                                                          
mqttc.loop_forever()