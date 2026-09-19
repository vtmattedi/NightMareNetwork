Device: 
* must publish status -> on/off -> handled by mqtt.cpp (Retained)
* console/in -> console/out
* console/controlled/`<id>`/in ->  console/controlled/`<id>`/out.
* Device/Resources -> ennumerate devices netResources
* Device/Resource/name -> publishes payload: sensorData or Action Result
