
#ifndef DB_HPP
#define DB_HPP

#include <map>
#include <vector>
#include <string>

typedef std::map<std::string, std::string> MapStr;
typedef std::vector<std::string> VecStr;
typedef std::map<std::string, VecStr> KeyValues;
typedef std::pair<MapStr, VecStr> KeyMapValue;
typedef std::map<int, std::vector<KeyMapValue> > GroupedDBMap;

struct DB
{
  const GroupedDBMap serversDB;
  const GroupedDBMap rootDB;
};

#endif