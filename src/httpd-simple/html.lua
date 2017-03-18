
local htmlPT = { br = true, hr = true, img = true, input = true }

function renderTemplate ( _tmpl )
	if ( type ( _tmpl ) == "table" ) then
		local cnt = ""
		for key, el in pairs ( _tmpl ) do
			if ( el[1] ~= nil ) then
				if ( #el > 1 ) then
					local tag = tostring ( el[1] )
					cnt = cnt .. "<" .. tag
					if ( type ( el[2] ) == "string" and el[2] ~= "" ) then
						cnt = cnt .. " " .. el[2]
					elseif ( type ( el[2] ) == "table" ) then
						for key, el in pairs ( el[2] ) do
							cnt = cnt .. " " .. key .. '="' .. el .. '"'
						end
					end
					cnt = cnt ..  ( ( htmlPT[tag] ~= nil ) and " />" or ">" .. ( ( el[3] ~= nil ) and renderTemplate ( el[3] ) or "" ) .. "</" .. tag .. ">" )
				else
					cnt = cnt .. renderTemplate ( el[1] )
				end
			end
		end
		return cnt
	else
		return tostring ( _tmpl )
	end
end

function renderHtmlPage ( _head, _body ) return renderTemplate ( { { "<!DOCTYPE html>" }, { "html", "", { { "head", "", _head }, { "body", "", _body } } } } ) end
function renderForm ( _action, _method, _content ) return renderTemplate ( { { "form", { action = _action, method = _method }, _content } } ) end

_username = "peter"
_password = "pass"

local loginForm = {
		{ "input", { name = "username", id = "username", type = "text", value = _username } },
		{ "input", { name = "password", id = "password", type = "password", value = _password } },
		{ "button", { name = "submit", type = "submit" }, "Login" }
	}

print ( renderHtmlPage ( "", renderForm ( "/url/new", "GET", loginForm ) ) )

-- print ( renderTemplate ( { { "img", { src = "/url/image.jpg", alt = "Big image" } } } ) )

