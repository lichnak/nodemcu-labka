//var mqtt=require('C:\\Users\\Adam Lichnovsky\\AppData\\Roaming\\npm\\node_modules\\mqtt\\mqtt.js')
var mqtt=require('mqtt')
var mongodb=require('mongodb');  
var mongodbClient=mongodb.MongoClient;  
var mongodbURI='mongodb://mqtt:PoXwMxqCLGxD794ZnH6B@localhost:27017/mqtt'
var deviceRoot="nodemcu/sensor/temperature"  
var collection,client;
mongodbClient.connect(mongodbURI,setupCollection);

function setupCollection(err,db) {  
  if(err) throw err;
  collection=db.collection("temperatures");
  client=mqtt.createClient(1883,'localhost')
  client.subscribe(deviceRoot+"+")
  client.on('message', insertEvent);
}

function insertEvent(topic,payload) {  
  var key=topic.replace(deviceRoot,'');
  
collection.update(  
  { _id:key },
  { $push: { events: { event: { value:payload, when:new Date() } } } },
  { upsert:true },
  function(err,docs) {
    if(err) { console.log("Insert fail"); }
  }
  )
}