#include "../../inc/AllHeaders.hpp"

RequestConfig::RequestConfig(HttpRequest &request, Listen &host_port, DB &db, Client &client) : request_(request), client_(client), host_port_(host_port), db_(db)
{
}

RequestConfig::~RequestConfig()
{
    cgi_.clear();
    error_codes_.clear();
}

const VecStr &RequestConfig::filterDataByDirectives(const std::vector<KeyMapValue> &targetServ, std::string directive, std::string location = "")
{
    std::string locationExtract;

    for (size_t i = 0; i < targetServ.size(); ++i)
    {
        const MapStr &keyMap = targetServ[i].first;
        const VecStr &valueVector = targetServ[i].second;

        MapStr::const_iterator directiveIt = keyMap.find("directives");
        if (directiveIt != keyMap.end() && directiveIt->second == directive)
        {
            MapStr::const_iterator locationIt = keyMap.find("location");
            if (locationIt != keyMap.end())
            {
                locationExtract = locationExtractor(locationIt->second);
                if (locationExtract == location)
                    return valueVector;
            }
        }
    }

    static VecStr emptyVector;
    return emptyVector;
}

bool RequestConfig::directiveExists(std::string directive, std::string location)
{
    for (size_t i = 0; i < targetServer_.size(); ++i)
    {
        const MapStr &keyMap = targetServer_[i].first;
        MapStr::const_iterator dirIt = keyMap.find("directives");
        MapStr::const_iterator locIt = keyMap.find("location");

        (void)directive;

        if (dirIt != keyMap.end() && locIt != keyMap.end())
        {
            std::string dir = dirIt->second;
            std::string loc = locIt->second;

            if (loc == location && dir == directive)
            {
                std::cout << "Checking: Directive = " << dir << ", Location = " << loc << std::endl;
                return true;
            }
        }
    }
    return false; // Directive not found in the specified location
}

const VecStr &RequestConfig::checkRootDB(std::string directive)
{
    GroupedDBMap::const_iterator it;
    for (it = db_.rootDB.begin(); it != db_.rootDB.end(); ++it)
    {
        const std::vector<ConfigDB::KeyMapValue> &values = it->second;
        for (size_t i = 0; i < values.size(); ++i)
        {
            const MapStr &keyMap = values[i].first;
            const VecStr &valueVector = values[i].second;
            std::string location = keyMap.find("location")->second;
            const VecStr &dirValue = filterDataByDirectives(values, directive, location);
            if (!dirValue.empty())
                return valueVector;
        }
    }

    static VecStr emptyVector;
    return emptyVector;
}

const VecStr &RequestConfig::cascadeFilter(std::string directive, std::string location = "")
{
    /// @note important to first pre-populate data in cascades:
    // 1. preffered settings
    // 2. http level
    // 3. server level(locatn == "" == server-default settings)
    // 4. location level

    const VecStr &dirValue = filterDataByDirectives(targetServer_, directive, location);
    if (!dirValue.empty())
        return dirValue;

    if (dirValue.empty() && !location.empty())
    {
        const VecStr &filteredValue = filterDataByDirectives(targetServer_, directive, "");
        if (!filteredValue.empty())
            return filteredValue;
    }

    return checkRootDB(directive);
}
std::string RequestConfig::locationExtractor(const std::string &locationStr)
{
    size_t j;

    for (j = 0; j < locationStr.size(); ++j)
    {
        char ch = locationStr[j];

        if (!(ch == '^' || ch == '*' || ch == '~' || ch == '=' || ch == '_'))
            break;
    }
    return locationStr.substr(j);
}

LocationModifier RequestConfig::checkModifier(const std::string &locationStr)
{
    LocationModifier modifierType_ = NONE;
    std::string modifiers;
    size_t pos = locationStr.find_first_of("/");
    if (pos != std::string::npos)
    {
        modifiers = locationStr.substr(0, pos);
    }
    else
    {
        return modifierType_;
    }

    std::set<char> validModifiers;
    validModifiers.insert('^');
    validModifiers.insert('*');
    validModifiers.insert('~');
    validModifiers.insert('=');
    validModifiers.insert('_');

    for (size_t j = 0; j < modifiers.size(); ++j)
    {
        char ch = modifiers[j];

        if (validModifiers.find(ch) == validModifiers.end())
            throw std::runtime_error("Invalid location modifier");
    }

    bool hasTilde = (modifiers.find("~") != std::string::npos);
    bool hasAsterisk = (modifiers.find("*") != std::string::npos);
    bool hasCaret = (modifiers.find("^") != std::string::npos);
    bool hasEquals = (modifiers.find("=") != std::string::npos);

    if (hasCaret && hasTilde)
        modifierType_ = LONGEST;
    else if (hasTilde && hasAsterisk)
        modifierType_ = CASE_INSENSITIVE;
    else if (hasEquals)
        modifierType_ = EXACT;
    else if (hasTilde)
        modifierType_ = CASE_SENSITIVE;

    return modifierType_;
}

/**
 * SETTERS
 */

RequestConfig *RequestConfig::getRequestLocation(std::string request_target)
{
    RequestConfig *requestConfig = NULL;
    // std::map<std::string,int regex_locations;
    std::map<std::string, int>::iterator it = locationsMap_.begin();
    while (it != locationsMap_.end())
    {
        // std::cout << "Modifiers: " << it->second << std::endl;
        // std::cout << "Uri: " << it->first << std::endl;
        // std::cout << "Target: " << request_target << std::endl;

        if (it->second != CASE_SENSITIVE && it->second != CASE_INSENSITIVE)
        {
            if (it->second == 1 && it->first == request_target)
                return &(*this);
            else if (request_target.find(it->first) == 0)
            {
                if (requestConfig && requestConfig->uri_.length() < it->first.length())
                    requestConfig = &(*this);
                else if (!requestConfig)
                    requestConfig = &(*this);
            }
        }
        // else
        // reg_locations.push_back(&(*it));

        it++;
    }
    if (requestConfig)
        std::cout << "RequestConfig: " << requestConfig->uri_ << std::endl;
    //   if (location && location->modifier_ == 4)
    // return location;

    // if (location && !location->locations_.empty())
    // {
    //     for (std::vector<ServerConfig>::iterator it = location->locations_.begin(); it != location->locations_.end(); it++)
    //     {
    //     if (it->modifier_ == 2 || it->modifier_ == 3)
    //         reg_locations.insert(reg_locations.begin(), &(*it));
    //     }
    // }
    (void)request_target;
    return requestConfig;
}

void RequestConfig::setLocationsMap(const std::vector<KeyMapValue> &values)
{
    int modifier = NONE;
    for (size_t i = 0; i < values.size(); ++i)
    {
        const MapStr &keyMap = values[i].first;
        std::string loc = keyMap.find("location")->second;
        modifier = checkModifier(loc);

        if (!loc.empty() && locationsMap_.find(loc) == locationsMap_.end())
        {
            locationsMap_[loc] = modifier;
        }
    }
}

void RequestConfig::returnRedirection() {
    std::map<int, std::string> m = getRedirectionMap();
    if (getRedirectionMap().size())
    {
        std::map<int, std::string>::const_iterator it = m.begin();
        for (it = m.begin(); it != m.end(); ++it)
            request_.setTarget(m[it->first]);
    }

}

void RequestConfig::setBestMatch(std::string &newTarget) {
    std::string target = request_.getTarget();
	std::string longestMatch = "";

	std::map<std::string, int>::const_iterator locationsMap = getLocationsMap().begin();
	while (locationsMap != getLocationsMap().end())
	{
		if (target.find(locationsMap->first) == 0 && locationsMap->first.length() > longestMatch.length())
			longestMatch = locationsMap->first;
		locationsMap++;
	}
	if (!longestMatch.empty())
	{
        newTarget = target.substr(0, longestMatch.length());
		target.erase(0, longestMatch.length());
		setTarget("/" + target);
		setUri("/" + target);
	} else {
        newTarget = request_.getTarget();
        setTarget(request_.getTarget());
        setUri(request_.getURI());
    }
}


void RequestConfig::setUp(size_t targetServerIdx)
{
    std::string newTarget;
    targetServer_ = getDataByIdx(db_.serversDB, targetServerIdx);
    serverId = targetServerIdx;

    setRedirectMap(cascadeFilter("return", request_.getTarget()));
    returnRedirection();
    setLocationsMap(targetServer_);
    setBestMatch(newTarget);
    setRoot(cascadeFilter("root", newTarget));
    setClientMaxBodySize(cascadeFilter("client_max_body_size", newTarget));
    setAutoIndex(cascadeFilter("autoindex", newTarget));
    setIndexes(cascadeFilter("index", newTarget));
    setErrorPages(cascadeFilter("error_page", newTarget));
    setMethods(cascadeFilter("allow_methods", newTarget));
    setAuth(cascadeFilter("auth", newTarget));
    setCgi(cascadeFilter("cgi", newTarget));
    setCgiBin(cascadeFilter("cgi-bin", newTarget));

    // RequestConfig *location = NULL;
    int status = request_.getStatus();
    if (status != 200 && status != 100)
    {
        // location = getRequestLocation(request_.getTarget());
    }
    std::cout << std::endl;
}

void RequestConfig::redirectLocation(std::string target)
{
    // RequestConfig *location = NULL;
    int status = request_.getStatus();
    if (status != 200 && status != 100)
    {
        // location = getRequestLocation(target);
    }
    target_ = target;
}

void RequestConfig::setTarget(const std::string &target)
{
    target_ = target;
}

void RequestConfig::setRoot(const VecStr root)
{

    root_ = root.empty() ? "html" : root[0];
}

void RequestConfig::setAuth(const VecStr &auth)
{
    auth_ = auth.empty() ? "off" : auth[0];
}

void RequestConfig::setUri(const std::string uri)
{
    uri_ = uri;
}

void RequestConfig::setClientMaxBodySize(const VecStr size)
{
    size_t val = size.empty() ? 20971520 : (std::istringstream(size[0]) >> val, val);
    client_max_body_size_ = val;
}

void RequestConfig::setUpload(const VecStr &upload)
{
    upload_ = upload[0];
}

void RequestConfig::setAutoIndex(const VecStr autoindex)
{
    autoindex_ = autoindex.empty() ? false : (autoindex[0] == "on");
}

void RequestConfig::setAutoIndex(bool autoindex)
{
    autoindex_ = autoindex;
}

void RequestConfig::setIndexes(const VecStr &indexes)
{
    indexes_ = indexes;
}

void RequestConfig::setMethods(const VecStr &methods)
{
    allowed_methods_ = methods.empty() ? cascadeFilter("limit_except", target_) : methods;
}

void RequestConfig::setCgi(const VecStr &cgi)
{
    // cgi_.clear();

    // if (cgi.size() % 2 != 0)
    //     std::cerr << "Warning: Cgi value is empty\n";

    // for (size_t i = 0; i < cgi.size(); i += 2)
    // {
    //     const std::string &key = cgi[i];
    //     std::string value;

    //     if (i + 1 < cgi.size())
    //         value = cgi[i + 1];
    //     cgi_[key] = value;
    // }
    cgi_ = cgi;
}

void RequestConfig::setCgiBin(const VecStr &cgi)
{
    cgi_bin_ = (cgi_bin_.empty()) ? "" : cgi[0];
}

void RequestConfig::assignCodes(const std::string &codes, const std::string &page, std::map<int, std::string> &resultMap)
{
    std::istringstream codeStream(codes);
    int code;
    while (codeStream >> code)
    {
        resultMap[code] = page;
    }
}

void RequestConfig::setMap(const VecStr &vec, std::map<int, std::string> &resultMap, std::string &codes)
{
    if (!vec.empty())
    {
        for (size_t i = 0; i < vec.size(); ++i)
        {
            std::istringstream iss(vec[i]);
            int code;
            if (iss >> code)
            {
                // Its a code. concatenate it
                if (!codes.empty())
                    codes += " ";
                codes += vec[i];
            }
            else
            {
                if (!codes.empty())
                {
                    assignCodes(codes, vec[i], resultMap);
                    codes.clear();
                }
            }
        }

        // Assign the last page to remaining codes
        if (!codes.empty())
        {
            assignCodes(codes, vec.back(), resultMap);
        }
    }
}

void RequestConfig::setErrorPages(const VecStr &errors)
{
    std::map<int, std::string> resultMap;
    std::string errorCodes;
    setMap(errors, resultMap, errorCodes);
    error_codes_ = resultMap;
}

void RequestConfig::setRedirectMap(const VecStr &redirectMap)
{
    std::map<int, std::string> resultMap;
    std::string redirCodes;
    setMap(redirectMap, resultMap, redirCodes);
    redirectMap_ = resultMap;
}


/**
 * GETTERS
 */

std::map<std::string, std::string> RequestConfig::getHeaders()
{
    std::map<std::string, std::string>::const_iterator it;
    MapStr headers = request_.getHeaders();

    return headers;
}

std::string &RequestConfig::getTarget()
{
    return target_;
}

std::string &RequestConfig::getRequestTarget()
{
    return request_.getURI();
}

std::string &RequestConfig::getQuery()
{
    return request_.getQuery();
}

std::string &RequestConfig::getFragment()
{
    return request_.getFragment();
}

std::string &RequestConfig::getMethod()
{
    return request_.getMethod();
}

std::string &RequestConfig::getHost()
{
    return host_port_.ip_;
}

uint32_t &RequestConfig::getPort()
{
    return host_port_.port_;
}

size_t RequestConfig::getContentLength()
{
    return request_.getContentLength();
}

Client &RequestConfig::getClient()
{
    return client_;
}

std::string &RequestConfig::getRoot()
{
    return root_;
}

std::string &RequestConfig::getUri()
{
    return uri_;
}

std::string &RequestConfig::getAuth()
{
    return auth_;
}

size_t &RequestConfig::getClientMaxBodySize()
{
    return client_max_body_size_;
}

bool RequestConfig::getAutoIndex()
{
    return autoindex_;
}

std::vector<std::string> &RequestConfig::getIndexes()
{
    return indexes_;
}

std::map<int, std::string> &RequestConfig::getErrorPages()
{
    return error_codes_;
}

std::map<int, std::string> &RequestConfig::getRedirectionMap()
{
    return redirectMap_;
}

std::vector<std::string> &RequestConfig::getMethods()
{
    return allowed_methods_;
}

std::string &RequestConfig::getBody()
{
    return request_.getBody();
}

std::string &RequestConfig::getHeader(std::string key)
{
    std::transform(key.begin(), key.end(), key.begin(), tolower);
    return request_.getHeader(key);
}

std::string &RequestConfig::getProtocol()
{
    return request_.getProtocol();
}

std::string &RequestConfig::getUpload()
{
    return upload_;
}

std::vector<std::string> &RequestConfig::getCgi()
{
    return cgi_;
}

std::string &RequestConfig::getCgiBin()
{
    return cgi_bin_;
}

std::map<std::string, int> &RequestConfig::getLocationsMap()
{
    return locationsMap_;
}

bool RequestConfig::isMethodAccepted(std::string &method)
{
    bool methodFlag = directiveExists("allow_methods", target_) || directiveExists("limit_except", target_);

    if (!methodFlag)
        return true;

    return (allowed_methods_[0] == "none" || method.empty()) ? false : (std::find(allowed_methods_.begin(), allowed_methods_.end(), method) != allowed_methods_.end());
}

void RequestConfig::printConfigSetUp()
{
    /// @note debugging purpose
    std::cout << "\nTarget: " << getTarget() << std::endl;
    std::cout << "\nURI: " << getUri() << std::endl;
    std::cout << "\nROOT: " << getRoot() << std::endl;
    std::cout << "\nHOST: " << getHost() << std::endl;
    std::cout << "\nPORT: " << getPort() << std::endl;
    std::cout << "\nMETHOD: " << getMethod() << std::endl;
    std::cout << "\nCLIENTMAXBODY: " << getClientMaxBodySize() << std::endl;
    std::cout << "\nAUTOINDEX: " << getAutoIndex() << std::endl;
    std::cout << getProtocol() << std::endl;
    std::cout << "\nBODY: " << getBody() << std::endl;
    std::cout << "\nINDEXES: \n";
    printVec(getIndexes(), "SETUP");
    std::cout << "\nERRORPAGES\n";
    printMap(getErrorPages());
    std::cout << "\nMETHODS\n";
    printVec(getMethods(), "SETUP");
    std::cout << "\nHEADERS\n";
    printMap(getHeaders());
    std::cout << std::endl;
    std::cout << "\nCGI\n";
    // printVec(cgi_);
    std::cout << std::endl;
    std::cout << "\nCGI-BIN: " << getCgiBin() << std::endl;

    std::cout << "\n[Accepted Method] " << isMethodAccepted(getMethod());
    std::cout << "\n[content-length] " << getContentLength() << "\n"
              << std::endl;
}
