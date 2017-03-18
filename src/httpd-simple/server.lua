local serverRouting = { 
    ["GET"] = {}, 
    ["POST"] = {}
}

local httpStatusCodes = {
    [200] = "OK",
    [201] = "Created",
    [202] = "Accepted",
    [301] = "Moved Permanently",
    [302] = "Found",
    [304] = "Not Modified",
    [400] = "Bad Request",
    [401] = "Unauthorized",
    [403] = "Forbidden",
    [404] = "Not Found",
    [405] = "Method Not Allowed",
    [406] = "Not Acceptable"
}

function setWifi ( _ssid, _pass )
    local ip, nm, gw

    print ( '\nwifi.lua initialization\n' )
    wifi.setmode ( wifi.STATION )
    
    print ( 'set mode=STATION (mode=' .. wifi.getmode() .. ')\n' )
    print ( 'MAC Address: ', wifi.sta.getmac() )
    print ( 'Chip ID: ', node.chipid() )
    print ( 'Heap Size: ', node.heap(), '\n' )

    wifi.sta.config ( _ssid, _pass )

    -- WiFi Connection Verification
    tmr.alarm ( 0, 1000, 1, function ()
            if wifi.sta.getip() == nil then
                print ( "Connecting to AP...\n")
            else
                ip, nm, gw = wifi.sta.getip()
                print ( "IP Info: \nIP Address: ", ip )
                print ( "Netmask: ", nm )
                print ( "Gateway Addr: ", gw, '\n' )
                tmr.stop(0)
            end
        end
    )
end

local unescape = function ( str )
  return str:gsub ( "%%(%x%x)", function ( x ) return string.char ( tonumber ( x, 16 ) ) end )
end

function parseQuery ( query )
    local queryData = {}
    local key, value
    local query = string.gsub ( query, "&amp;", "&" )
    
    for str in string.gmatch ( query, "([^&]+)" ) do
        key, value = string.match ( str, "^([^=]+)=*(.*)$" )
        if ( key ~= nil )  then
            queryData[ key ] = value
        end
    end
    return queryData
end

function stringTrim(s)
  return (s:gsub ("^%s*(.-)%s*$", "%1"))
end

function parsePayload ( payload )
    local payloadData = { ["HEADER"] = {}, ["CONTENT"] = "" }
    local header = { 
        ["METHOD"] = "", 
        ["REQUEST_FULL"] = "", 
        ["PROTOCOL"] = "",
        ["PATH"] = "",
        ["QUERY"] = "",
        ["GET"] = {},
        ["POST"] = {},
    }
    local content = ""
    local query, key, value
    local first = nil
    local isHeader = 1

    for str in string.gmatch ( payload, "([^\n]+)" ) do
      
        -- First line with method and path
        if ( first == nil ) then  
            first = 1
            header["METHOD"], header["REQUEST_FULL"], header["PROTOCOL"] = string.match ( str, "^([^%s]+)%s+([^%s]+)%s+(HTTP/.*)" )
            header["REQUEST_FULL"] = unescape ( header["REQUEST_FULL"] )

        elseif ( stringTrim ( str ) == "" ) then
            isHeader = nil
        
        elseif ( isHeader == 1 ) then
            key, value = string.match ( str, "^%s*([^:]+):%s*(.*)%s*$" )
            if ( key ~= nil ) then
               header[ string.upper ( key ) ] = value
            end
        elseif ( isHeader ~= 1 ) then
            if ( content  ~= "" ) then
                content = content .. "\n"
            end
            content = content .. str
        end
    end

    if ( header["REQUEST_FULL"] ~= "" ) then
        header["PATH"], query = string.match ( header["REQUEST_FULL"], "^([^%?]+)%?*(.*)$" )
        header["QUERY"] = parseQuery ( query )
        if ( header["METHOD"] == "GET" ) then
            header["GET"] = header["QUERY"]
        end
    end

    if ( header["METHOD"] == "POST" ) then
        header["POST"] = parseQuery ( content )
    end

    payloadData["HEADER"] = header
    payloadData["CONTENT"] = content

    return payloadData

end

function serverResponse ( status, contentType, content )
    local statusReason = ""

    if ( httpStatusCodes[state] ~= nil ) then 
        statusReason = httpStatusCodes[state]
    end

    local response = "HTTP/1.1 " .. status .. " " .. statusReason .. "\r\n"
    response = response .. "Content-Length: " .. string.len ( content ) .. "\r\n\r\n"
    response = response .. content
    return response
end

function onServerReceive ( connection, payload )

    -- print ( "Payload received: " .. string.len( payload ) .. " bytes" )
    
    local method
    local request = parsePayload ( payload )
    local response = serverResponse ( 200, "text/plain", "defaultResponse" )
    headerMethod = request["HEADER"]["METHOD"]
    headerPath   = request["HEADER"]["PATH"]

    print ( "\nRequest Info" )
    print ( "Method: ", headerMethod )
    print ( "Path: ", headerPath )
    print ( "Query: ", request["HEADER"]["QUERY"] )
    
    if ( headerMethod == "GET" or headerMethod == "POST" ) then
        for path, callback in pairs( serverRouting[ headerMethod ] ) do
            if ( path == headerPath ) then
                response = callback ( request )
                break
            end
        end
    end
    
    connection:send ( response )
    connection:on ( "sent", function(sck) sck:close() end )
end

function onGet ( path, callback )
    serverRouting["GET"][path] = callback
end

function onPost ( path, callback )
    serverRouting["POST"][path] = callback
end

-- Connection function
function serverConnection ( conn ) 
    conn:on ( "receive", onServerReceive )
end

function startServer ( _port )
    local srv
    
    print ( "\nStarting Web Server...\n" )
    -- Create a server object with 30 second timeout
    srv = net.createServer ( net.TCP, 30 ) 
    srv:listen ( _port, serverConnection )

    return srv
end