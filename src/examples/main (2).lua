-- main.lua --

----------------------------------
-- WiFi Connection Verification --
----------------------------------
tmr.alarm(0, 1000, 1, function()
    if wifi.sta.getip() == nil then
        print("Connecting to AP...\n")
    else
        ip, nm, gw=wifi.sta.getip()
        print("IP Info: \nIP Address: ",ip)
        print("Netmask: ",nm)
        print("Gateway Addr: ",gw,'\n')
        tmr.stop(0)
    end
end)
-----------------
-- DS18B20 ini --
-----------------
th4 = require("ds18b20")
th4.setup(4)
addrs = th4.addrs()
if (addrs ~= nil) then
print("Total DS18B20 sensors: "..table.getn(addrs))
print("Temperature: " ..th4.readNumber())
tempC = th4.readNumber()
end

----------------
-- Web Server --
----------------
print("Starting Web Server...")
-- Create a server object with 30 second timeout
srv = net.createServer(net.TCP, 30)

-- server listen on 80, 
-- if data received, print data to console,
-- then serve up a sweet little website
srv:listen(8080,function(conn)
    conn:on("receive", function(conn, payload)
        print(payload) -- Print data from browser to serial terminal
    
        function esp_update()
            mcu_do=string.sub(payload,postparse[2]+1,#payload)

            if mcu_do == "Update" then 
                tempC = th4.readNumber()
            end
        end

        --parse position POST value from header
        postparse={string.find(payload,"mcu_do=")}
        --If POST value exist, read temperature
        if postparse[2]~=nil then esp_update() end

        -- CREATE WEBSITE --
        -- HTML Header Stuff
        conn:send('HTTP/1.1 200 OK\n\n')
        conn:send('<!DOCTYPE HTML>\n')
        conn:send('<html>\n')
        conn:send('<head><meta  content="text/html; charset=utf-8">\n')
        conn:send('<title>Lichnaks Home  DS18B20 thermometer</title></head>\n')
        conn:send('<body><h1>Lichnaks Home  DS18B20 thermometer</h1>\n')

        -- Labels
        conn:send('<p>Temperature: '..tempC..' Celsius</p><br>\n')
        conn:send('<p>or</p>\n')
        conn:send('<p>Temperature: '..(tempC * 9 / 5 + 32)..' Fahrenheit</p><br>\n')

        -- Buttons 
        conn:send('<form action="" method="POST">\n')
        conn:send('<input type="submit" name="mcu_do" value="Update">\n')
        conn:send('</body></html>\n')
        conn:close()
    end)
    tempC = nil
    conn:on("sent", function(conn) conn:close()
    end)
end)
