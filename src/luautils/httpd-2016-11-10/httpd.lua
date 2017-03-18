
print ( "before httpd", node.heap() )

require ( "ext_mini_server_httpd" )

print ( "after httpd", node.heap() )


serverOn ( "GET", "/", function ( req )
        return 200, "text/plain", "hello world"
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

serverOn ( "GET", "/led1/on", function ( dataType, data ) -- //http://192.168.1.x/led1/on
        local state = setLed ( 1, 1 )
        return 200, "text/plain", "LED 1 STATE: " .. state
    end 
)

serverOn ( "GET", "/led1/off", function ( dataType, data ) -- //http://192.168.1.x/led1/off
        local state = setLed ( 1, 0 )
        return 200, "text/plain", "LED 1 STATE: " .. state
    end 
)

serverOn ( "GET", "/led1/switch", function ( dataType, data ) -- //http://192.168.1.x/led1/switch
        local state = switchLed ( 1 )
        return 200, "text/plain", "LED 1 STATE: " .. state
    end 
)

serverOn ( "GET", "/led/on", function ( dataType, data ) -- //http://192.168.1.x/led/on?pin=1&state=1
        if dataType == "get" and data.pin and data.state then
            local state = setLed ( tonumber ( data.pin ), tonumber ( data.state ) )
            return 200, "text/plain", "LED " .. tonumber ( data.pin ) .. " STATE: " .. tonumber(state)
        else
           return 400
        end
    end 
)

serverOn ( "POST", "/led/on", function ( dataType, data ) -- //http://192.168.1.x/led/on?pin=1&state=1
        local query = req.data
        local state = ""
        if ( query["pin"] ~= nil and query["state"] ~= nil) then
            state = setLed ( tonumber ( query["pin"] ), tonumber ( query["state"] ) )
        end
        return 200, "text/plain", "LED " .. tostring ( query["pin"] ) .. " STATE: " .. tostring(state)
    end 
)

print ( "before httpd start", node.heap() )
server = serverStart ( ) 
print ( "after httpd start", node.heap() )
