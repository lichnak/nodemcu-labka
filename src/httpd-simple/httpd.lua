require ( "server" )

local ssid = "WIFINET"
local pass = "YOUR_PASSWORD"
local port = 80

setWifi ( ssid, pass )

onGet ( "/", function ( req )
        return serverResponse ( 200, "text/plain", "hello world" )
    end 
)

function setLed ( pin, value )
    gpio.mode ( pin, gpio.OUTPUT )
    if value == 1 then
        gpio.write ( pin,gpio.HIGH )
    else
        gpio.write ( pin, gpio.LOW )
    end
    local state = gpio.read ( pin )
    -- print ( "LED(" .. pin .. "):", state )
    return state
end

function switchLed ( pin )
    if gpio.read ( pin ) == 1 then
        return setLed ( pin, 0 )
    else
        return setLed ( pin, 1 )
    end
end

onGet ( "/led1/on", function ( req ) -- //http://192.168.1.x/led1/on
        local state = setLed ( 1, 1 )
        return serverResponse ( 200, "text/plain", "LED 1 STATE: " .. state )
    end 
)

onGet ( "/led1/off", function ( req ) -- //http://192.168.1.x/led1/off
        local state = setLed ( 1, 0 )
        return serverResponse ( 200, "text/plain", "LED 1 STATE: " .. state )
    end 
)

onGet ( "/led1/switch", function ( req ) -- //http://192.168.1.x/led1/switch
        local state = switchLed ( 1 )
        return serverResponse ( 200, "text/plain", "LED 1 STATE: " .. state )
    end 
)

onGet ( "/led/on", function ( req ) -- //http://192.168.1.x/led/on?pin=1&state=1
        local query = req["HEADER"]["QUERY"]
        local state
        if ( query["pin"] ~= nil and query["state"] ~= nil) then
            state = setLed ( tonumber ( query["pin"] ), tonumber ( query["state"] ) )
        end
        return serverResponse ( 200, "text/plain", "LED " .. tonumber ( query["pin"] ) .. " STATE: " .. state )
    end 
)

server = startServer ( port )


