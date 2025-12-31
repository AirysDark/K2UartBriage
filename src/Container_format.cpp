#include "Container_format.h"
#include "Debug.h"

DBG_REGISTER_MODULE(__FILE__);

static void wr_u16(std::vector<uint8_t>& o, uint16_t v){ o.push_back(v&0xFF); o.push_back((v>>8)&0xFF); }
static void wr_u32(std::vector<uint8_t>& o, uint32_t v){
  o.push_back(v&0xFF); o.push_back((v>>8)&0xFF); o.push_back((v>>16)&0xFF); o.push_back((v>>24)&0xFF);
}
static bool rd_u16(const uint8_t* b, size_t n, size_t& off, uint16_t& out){
  if (off+2>n) return false;
  out = (uint16_t)b[off] | ((uint16_t)b[off+1]<<8);
  off+=2; return true;
}
static bool rd_u32(const uint8_t* b, size_t n, size_t& off, uint32_t& out){
  if (off+4>n) return false;
  out = (uint32_t)b[off] | ((uint32_t)b[off+1]<<8) | ((uint32_t)b[off+2]<<16) | ((uint32_t)b[off+3]<<24);
  off+=4; return true;
}

void BackupContainer::clear(){ _sections.clear(); }

void BackupContainer::add(const String& name, const uint8_t* data, size_t len){
  BackupSection s;
  s.name = name;
  s.data.assign(data, data+len);
  _sections.push_back(std::move(s));
}

BackupSection* BackupContainer::get(const String& name){
  for (auto &s : _sections) if (s.name == name) return &s;
  return nullptr;
}

bool BackupContainer::serialize(std::vector<uint8_t>& out) const{
  out.clear();
  const char magic[4] = {'K','2','B','K'};
  out.insert(out.end(), magic, magic+4);
  wr_u32(out, 1);
  wr_u32(out, (uint32_t)_sections.size());
  for (auto &s : _sections){
    auto nb = s.name.c_str();
    uint16_t nl = (uint16_t)strlen(nb);
    wr_u16(out, nl);
    out.insert(out.end(), (const uint8_t*)nb, (const uint8_t*)nb + nl);
    wr_u32(out, (uint32_t)s.data.size());
    out.insert(out.end(), s.data.begin(), s.data.end());
  }
  return true;
}

bool BackupContainer::deserialize(const uint8_t* buf, size_t len){
  clear();
  if (len < 12) return false;
  if (!(buf[0]=='K' && buf[1]=='2' && buf[2]=='B' && buf[3]=='K')) return false;
  size_t off=4;
  uint32_t ver=0, cnt=0;
  if(!rd_u32(buf,len,off,ver)) return false;
  if(!rd_u32(buf,len,off,cnt)) return false;
  if (ver != 1) return false;

  for (uint32_t i=0;i<cnt;i++){
    uint16_t nl=0; uint32_t dl=0;
    if(!rd_u16(buf,len,off,nl)) return false;
    if(off+nl>len) return false;
    String name((const char*)(buf+off), nl);
    off += nl;
    if(!rd_u32(buf,len,off,dl)) return false;
    if(off+dl>len) return false;
    add(name, buf+off, dl);
    off += dl;
  }
  return true;
}
