-- This Source Code Form is subject to the terms of Creative Commons
-- Attribution-ShareAlike 4.0 International License
-- https://creativecommons.org/licenses/by-sa/4.0/
-- Assemble.lua script
-- Author: peterbay (Petr Vavrin) pb.pb(at)centrum.cz
for k,v in pairs(file.list()) do 
 local p,n=string.match(k,"^(.*)__(.*)$");
 if p and n then
  print("remove: "..p.."/"..n)
  file.remove (p.."/"..n)
  print("rename: "..k.." -> "..p.."/"..n)
  file.rename(k,p.."/"..n) 
 end 
 p,n=nil,nil 
end

local buildPage = function(_filename,_node_name,_title,_links,_content) 
 if file.open("page.tmpl") then
  tmpl = file.read()
  file.close()
  if file.open(_content,"r") then
   print("load content file: ".._content)
   _content=file.read(2000)
   file.close()
  end
  if file.open(_filename,"w") then
   print("build: ".._filename)
   for part in string.gmatch(tmpl,"([^%|]+)") do
    if     part=="TITLE"       then file.write(_title)
    elseif part=="NAME"   then file.write(_node_name)
    elseif part=="LINKS"  then file.write(_links)
    elseif part=="CONTENT"     then file.write(_content)
    else                            file.write(part)
    end
   end
   file.close()
  end
 end
 _content=nil
end

local framework={
 {"static/index.htm", "Home", "index.tmpl"},
 {"static/api.htm", "Api", "api.tmpl"},
 {"static/gpio.htm", "GPIO", "gpio.tmpl"},
 {"static/wifi.htm", "WIFI", "wifi.tmpl"},
 {"static/console.htm", "RC", "console.tmpl"},
 {"static/about.htm", "About", "about.tmpl"},
 {"static/edit.htm", "Edit", "edit.tmpl"},
}
-- build --
local node_name = "NODE1"

for k,v in pairs(framework)do
 if v[1] and v[2] and v[3] then
  local links=""
  if v[1] and v[1] ~= "" then
   for kL,vL in pairs(framework) do
    if vL[1] and vL[1] ~= "" then
      links=links..'<a href="/'..vL[1]..'" '..(k==kL and 'class="cur"' or '')..'>'..vL[2]..'</a>'
    end
   end
  end
  buildPage(v[1],node_name,v[2],links,v[3])
  links = nil
 end
end

if httpd and httpd.resetEtag then
 httpd.resetEtag()
end

buildPage,framework=nil
