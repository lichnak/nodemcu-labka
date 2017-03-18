-- init.lua --

print ( utils.uri.decode ( "ahoj%20jak+se%32mas%32e" ) )

print ("----------------------------------------")

local httpRequest = "GET /source/4.0/lauxlib.c.html?uiyiuyu=ioio&jkhkj=8989 HTTP/1.1\r\nHost: www.lua.org\r\n"
--local httpRequest = "GET /source/4.0/lauxlib.c.html HTTP/1.1\r\nHost: www.lua.org\r\n"
httpRequest = httpRequest .. "User-Agent: Mozilla/5.0 (Windows NT 10.0; WOW64; rv:49.0) Gecko/20100101 Firefox/49.0\r\n"
httpRequest = httpRequest .. "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
httpRequest = httpRequest .. "Accept-Language: cs,en-US;q=0.7,en;q=0.3\r\n"
httpRequest = httpRequest .. "Accept-Encoding: gzip, deflate, br\r\n"
httpRequest = httpRequest .. "Referer: https://www.google.cz/\r\n"
httpRequest = httpRequest .. "Connection: keep-alive\r\n"
httpRequest = httpRequest .. "Upgrade-Insecure-Requests: 1\r\n"
httpRequest = httpRequest .. "If-Modified-Since: Sat, 05 Mar 2016 12:29:30 GMT\r\n"
httpRequest = httpRequest .. "If-None-Match: \"56dad12a=2b4b\"\r\n"
httpRequest = httpRequest .. "Cache-Control: max-age=0\r\n\r\ngg:iiiiasda sd asd as d ad\r\n"

local method, path, query, proto, ext, headers, body = utils.request.parse( httpRequest )

print("METHOD:", method)
print("PATH:", path)
print("QUERY:", query)
print("PROTO:", proto)
print("EXTENSION:", ext)

print ("----------------------------------------")
print ( "HEADER VALUES: " )
for k,v in pairs( headers ) do
    print ( k,"=", v)

end

print ("----------------------------------------")

print("BODY")
print("CONTENT:", body.content)
print("LENGTH:", body.length)


--require ( "ext_wifi" )
--wifiInit()
--dofile ( "httpd.lua" )
