-- main.lua --
print('\nLichnak main.lua\n')

-- WiFi Connection Verification
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

-- read DS18B20
function readTemperature(pin, unit)
    local th = nil
    th = require("ds18b20")
    th.setup(pin)
    local addrs = th.addrs()
    if (addrs == nil) then
        print("None DS18B20 sensors detected: "..table.getn(addrs))
    end
    local t = th.readNumber(addrs[1], unit)
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

--switch LED
function switchLED(pin)
--pin=1
gpio.mode(pin, gpio.OUTPUT)
    if gpio.read(pin) then
        gpio.write(pin, gpio.LOW)
        print("LED was turned off: " ..gpio.read(pin))
    else
        gpio.write(pin,gpio.HIGH)
        print("LED was turned on: " ..gpio.read(pin))
    end
end

 
-- Web Server --
a = 0

function receive(conn, payload)
    print(payload) 
    a = a + 1

    local content="<!DOCTYPE html><html><head><link rel='icon' type='image/png' href='http://nodemcu.com/favicon.png' /></head><body><h1>Hello!</h1><p>Since the start of the server " .. a .. " connections were made</p><br><p>Temperature: '..readTemperature(4,'C')..'&#8451</p></h1><br><a href="javascript:history.go(0)">Click to refresh the page</a></body></html>"
    local contentLength=string.len(content)

    conn:on("sent", function(sck) sck:close() end)
    conn:send("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length:" .. contentLength .. "\r\n\r\n" .. content)
end

function connection(conn) 
    conn:on("receive", receive)
end

print("Starting Web Server...")
-- Create a server object with 30 second timeout
srv=net.createServer(net.TCP, 30) 
srv:listen(8080, connection)














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


----------------------
-- Global Variables --
----------------------
led_pin = 1
sw_pin = 2
adc_id = 0 -- Not really necessary since there's only 1 ADC...
adc_value = 512

-- Amy from Gargantia on the Verdurous Planet
blink_open = "http://i.imgur.com/kzt3tO8.png"
blink_close = "http://i.imgur.com/KS1dPa7.png"
site_image = blink_open

----------------
-- GPIO Setup --
----------------
print("Setting Up GPIO...")
print("LED")
-- Inable PWM output
pwm.setup(led_pin, 2, 512) -- 2Hz, 50% duty default
pwm.start(led_pin)

print("Switch")
-- Enable input
gpio.mode(sw_pin, gpio.INPUT)

----------------
-- Web Server --
----------------
print("Starting Web Server...")
-- Create a server object with 30 second timeout
srv = net.createServer(net.TCP, 30)

-- server listen on 80, 
-- if data received, print data to console,
-- then serve up a sweet little website
srv:listen(80,function(conn)
	conn:on("receive", function(conn, payload)
		--print(payload) -- Print data from browser to serial terminal
	
		function esp_update()
            mcu_do=string.sub(payload,postparse[2]+1,#payload)
            
            if mcu_do == "Update+LED" then 
            	if gpio.read(sw_pin) == 1 then
            		site_image = blink_open
            		-- Adjust freq
            		pwm.setclock(led_pin, adc_value)
            		print("Set PWM Clock")	
        		elseif gpio.read(sw_pin) == 0 then
        			site_image = blink_close
        			-- Adjust duty cycle
        			pwm.setduty(led_pin, adc_value)
        			print("Set PWM Duty")	
            	end		
            end
            
            if mcu_do == "Read+ADC" then
            	adc_value = adc.read(adc_id)
            	-- Sanitize ADC reading for PWM
				if adc_value > 1023 then
					adc_value = 1023
				elseif adc_value < 0 then
					adc_value = 0
				end
				print("ADC: ", adc_value)
            end
        end

        --parse position POST value from header
        postparse={string.find(payload,"mcu_do=")}
        --If POST value exist, set LED power
        if postparse[2]~=nil then esp_update()end


		-- CREATE WEBSITE --
        
        -- HTML Header Stuff
        conn:send('HTTP/1.1 200 OK\n\n')
        conn:send('<!DOCTYPE HTML>\n')
        conn:send('<html>\n')
        conn:send('<head><meta  content="text/html; charset=utf-8">\n')
        conn:send('<title>ESP8266 Blinker Thing</title></head>\n')
        conn:send('<body><h1>ESP8266 Blinker Thing!</h1>\n')
       	
        -- Images... just because
        conn:send('<IMG SRC="'..site_image..'" WIDTH="392" HEIGHT="196" BORDER="1"><br><br>\n')

        -- Labels
        conn:send('<p>ADC Value: '..adc_value..'</p><br>')
        conn:send('<p>PWM Frequency (Input High): '..adc_value..'Hz</p>')
	conn:send('<p>or</p>')
        conn:send('<p>PWM Duty Cycle (Input Low): '..(adc_value * 100 / 1024)..'%</p><br>')

       	-- Buttons 
       	conn:send('<form action="" method="POST">\n')
        conn:send('<input type="submit" name="mcu_do" value="Read ADC">\n')
        conn:send('<input type="submit" name="mcu_do" value="Update LED">\n')
        conn:send('</body></html>\n')
        conn:on("sent", function(conn) conn:close() end)
	end)
end)