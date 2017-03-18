-- gpio1 = led_red, gpio2 = led_green
-- Read DS18B20 with index 1 on gpio4
function getTemperature(pin, unit)
    local th = nil
    th = require("ds18b20")
    th.setup(pin)
    local addrs = th.addrs()
    if (addrs == nil) then
        print("None DS18B20 sensors detected: "..table.getn(addrs))
    end
   
    local t = th.readNumber(addrs[1], unit)
    while t == 85 do
        local t = th.readNumber(addrs[1], unit)
    end
    if (t == nil) then
      return nil
    else
      return t
    end
    --Unload DS18B20
    th = nil
    ds18b20 = nil
    package.loaded["ds18b20"]=nil
end

local node_id = 'nodemcu_'..node.chipid()

-- init mqtt client with keepalive timer 120sec
m = mqtt.Client(node_id, 30, "", "")

-- setup Last Will and Testament (optional)
-- Broker will publish a message with qos = 0, retain = 0, data = "offline" 
-- to topic "/lwt" if client don't send keepalive packet
m:lwt("/lwt", "offline", 0, 0)

m:on("connect", function(client) print ("connected") end)
m:on("offline", function(client) print ("offline") end)

-- on publish message receive event
m:on("message", function(client, topic, data) 
  print(topic .. ":" ) 
  if data ~= nil then
    print(data)
  end
end)
--m:close();
m:connect("10.0.0.105", 1883, 0, function(client) 
--m:connect("192.168.1.219", 1883, 0, function(client) 

print("connected") 

tmr.alarm(0, 5000, tmr.ALARM_AUTO, function() 

local h = "86"
local t = getTemperature(4,"C")
local hi = "10"
local l = "44"
--local json = '{"_id" : " ' ..node_id.. '", "data" : {"humidity":"'..h..',"temperature":"'..t..'","heatindex":"'..hi..'","light":"'..l..'"}}'
local json = '{"data" : {"humidity":"'..h..'","temperature":"'..t..'","heatindex":"'..hi..'","light":"'..l..'"}}'

m:publish("/sensors/iolcity/weather/json",tostring(json),0,0)
m:publish("/sensors/iolcity/weather/humidity",tostring(h),0,0)
m:publish("/sensors/iolcity/weather/temperature",tostring(t),0,0)
m:publish("/sensors/iolcity/weather/heatindex",tostring(hi),0,0)
m:publish("/sensors/iolcity/weather/light",tostring(l),0,0)

--m:publish("/nodemcu/sensor/temperature",tostring(t),0,0)
--m:publish("/nodemcu/sensor/temperature","hello",0,0, function(client) print("sent") end)
end)


end, function(client, reason) print("failed reason: "..reason) end)


-- Calling subscribe/publish only makes sense once the connection
-- was successfully established. In a real-world application you want
-- move those into the 'connect' callback or make otherwise sure the 
-- connection was established.

-- subscribe topic with qos = 0
-- m:subscribe("/nodemcu/sensor/temperature",0, function(client) print("subscribe success") end)
-- publish a message with data = hello, QoS = 0, retain = 0


--m:close();
-- you can call m:connect again



--function sendMqttMessage()
--    
--end
--mqtt.Client(MQTT_CLIENTID, 60, "", "")

--m:lwt("/lwt", "Oh noes! Plz! I don't wanna die!", 0, 0)

-- When client connects, print status message and subscribe to cmd topic
--m:on("connect", function(m) 
    -- Serial status message
--    print ("\n\n", MQTT_CLIENTID, " connected to MQTT host ", MQTT_HOST,
--        " on port ", MQTT_PORT, "\n\n")

-- Subscribe to the topic where the ESP8266 will get commands from
--m:subscribe("/mcu/cmd/#", 0,
--    function(m) print("Subscribed to CMD Topic") end)
--end)
