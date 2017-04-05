// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
// Also add information on how to contact you by electronic and paper mail.

// ISIR 2015 Antoine Hoarau <hoarau.robotics@gmail.com>

#include "ati_sensor/ft_sensor.h"

#ifndef HAVE_RTNET

// XML related libraries
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include <sstream>
#include <vector>
#include <string>

#define rt_dev_socket socket
#define rt_dev_setsockopt setsockopt
#define rt_dev_bind bind
#define rt_dev_recvfrom recvfrom
#define rt_dev_sendto sendto
#define rt_dev_close close
#define rt_dev_connect connect
#define rt_dev_recv recv
#define rt_dev_send send

#else

// Give RTnet capabilities
#include <rtnet.h>
#include <rtdm/rtdm.h>

#endif

#ifndef HAVE_RTNET
// Read elements from XML file
static void findElementRecusive(xmlNode * a_node,const std::string element_to_find,std::string&  ret)
{
  xmlNode *cur_node = NULL;
  xmlNode *cur_node_temp = NULL;
  
  //xmlChar parameter_comp[40];
  for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
         std::string parameter_comp(reinterpret_cast<const char*>(cur_node->name));

         if(parameter_comp == element_to_find){
                cur_node_temp=cur_node->children;
                ret = std::string(reinterpret_cast<const char*>(cur_node_temp->content));
                continue;
         }
    }
    findElementRecusive(cur_node->children,element_to_find,ret);
  }
  return;
}
#endif

static std::string getStringInXml(const std::string& xml_s,const std::string& tag)
{
    const std::string tag_open = "<"+tag+">";
    const std::string tag_close = "</"+tag+">";
    const std::size_t n_start = xml_s.find(tag_open);
    const std::size_t n_end = xml_s.find(tag_close);
    return xml_s.substr(n_start+tag_open.length(),n_end);
}
template<typename T>
static T getNumberInXml(const std::string& xml_s,const std::string& tag)
{
    const std::string num = getStringInXml(xml_s,tag);
    double r = ::atof(num.c_str());
    return static_cast<T>(r);
}
template<typename T>
static bool getArrayFromString(const std::string& str,const char delim,T *data,size_t len)
{
    size_t start = str.find_first_not_of(delim), end=start;
    size_t idx = 0;
    while (start != std::string::npos && idx < len){
        end = str.find(delim, start);
        std::string token = str.substr(start, end-start);
        if (token.empty())
          token = "0.0";
        double r = ::atof(token.c_str());
        data[idx] = static_cast<T>(r); 
        ++idx;
        start = str.find_first_not_of(delim, end);
    }
    return (idx == len);
}
template<typename T>
static bool getArrayFromXml(const std::string& xml_s,const std::string& tag,const char delim,T *data,size_t len)
{
    const std::string str = getStringInXml(xml_s,tag);
    return getArrayFromString<T>(str,delim,data,len);
}


using namespace ati;

FTSensor::FTSensor()
{
    //  Default parameters
    initialized_                = false;
    ip                          = ati::default_ip;
    port                        = command_s::DEFAULT_PORT;
    cmd_.command                = command_s::STOP;
    cmd_.sample_count           = 1;
    calibration_index           = ati::current_calibration;
    socketHandle_               = -1;
    resp_.cpf                   = 1000000;
    resp_.cpt                   = 1000000;
    rdt_rate_                   = 0;
    timeval_.tv_sec             = 2;
    timeval_.tv_usec            = 0;
    xml_s_.reserve(MAX_XML_SIZE);
    setbias_ = new int[6];
}

FTSensor::~FTSensor()
{
  stopStreaming();
  if( 0 == closeSockets())
      std::cout << "Sensor shutdown sucessfully" << std::endl;
  delete setbias_;
}

bool FTSensor::startStreaming(int nb_samples)
{
  if (nb_samples < 0) {
    // use default sample_count
    return startStreaming();
  }
  else {
    // use given sample count
    uint32_t sample_count = static_cast<uint32_t>(nb_samples);
      switch(cmd_.command){
        case command_s::REALTIME:
    //std::cout << "Starting realtime streaming" << std::endl;
    return startRealTimeStreaming(sample_count);
        case command_s::BUFFERED:
    //std::cout << "Starting buffered streaming" << std::endl;
    return startBufferedStreaming(sample_count);
        case command_s::MULTIUNIT:
    //std::cout << "Starting multi-unit streaming" << std::endl;
    return startMultiUnitStreaming(sample_count);
        default:
    std::cout <<cmd_.command<< ": command mode not allowed"<< std::endl;
    return false;
      }
  }
}

// Initialization read from XML file
bool FTSensor::startStreaming()
{
    switch(cmd_.command){
      case command_s::REALTIME:
	//std::cout << "Starting realtime streaming" << std::endl;
	return startRealTimeStreaming();
      case command_s::BUFFERED:
	//std::cout << "Starting buffered streaming" << std::endl;
	return startBufferedStreaming();
      case command_s::MULTIUNIT:
	//std::cout << "Starting multi-unit streaming" << std::endl;
	return startMultiUnitStreaming();
      default:
	std::cout <<cmd_.command<< ": command mode not allowed"<< std::endl;
	return false;
    }
}

bool FTSensor::init(std::string ip, int calibration_index, uint16_t cmd, int sample_count)
{
  //  Re-Initialize parameters
  initialized_ = true;
  this->ip = ip;
  this->port = command_s::DEFAULT_PORT;
  cmd_.command = command_s::STOP;
  cmd_.sample_count = 1;
  this->calibration_index = calibration_index;
  
  //  Open Socket
  if(!ip.empty() && openSockets())
  {
#ifndef HAVE_RTNET
	std::cout << "Initializing ft sensor (gnulinux) "<< std::endl;
        if (rt_dev_setsockopt(socketHandle_, SOL_SOCKET, SO_RCVTIMEO,&timeval_,sizeof(timeval_)) < 0)
            std::cerr << "Error setting timeout" << std::endl;
#else
	std::cout << "Initializing ft sensor (xenomai + rtnet)"<< std::endl;
        nanosecs_rel_t timeout = (long long)timeval_.tv_sec*1E9 + (long long)timeval_.tv_usec*1E3;
        if( rt_dev_ioctl(socketHandle_, RTNET_RTIOC_TIMEOUT, &timeout) < 0)
            std::cerr << "Error setting timeout" << std::endl;
#endif
    if(!stopStreaming()) // if previously launched
        std::cerr << "\033[1;31mCould not stop streaming\033[0m" << std::endl;
    setCommand(cmd); // Setting cmd mode
    
    initialized_ &= startStreaming(sample_count);            // Starting streaming
    
    if (!initialized_)
        std::cerr << "\033[1;31mCould not start streaming\033[0m" << std::endl;
    
    initialized_ &= getResponse();
    
    if (!initialized_)
        std::cerr << "\033[1;31mCould not get response\033[0m" << std::endl;
      // Parse Calibration from web server
    if(initialized_)
        getCalibrationData();
  }else
    initialized_ = false;
  
  if (!initialized_)
    std::cerr << "\033[1;31mError during initialization, FT sensor NOT started\033[0m" << std::endl;
  
  return initialized_;
}
bool FTSensor::openSockets()
{
  // To get the online configuration (need to build rtnet with TCP option)
  openSocket(socketHTTPHandle_,getIP(),80,IPPROTO_TCP);
  // The data socket
  openSocket(socketHandle_,getIP(),getPort(),IPPROTO_UDP);
  
  return socketHTTPHandle_ !=-1 && socketHandle_ !=-1;
}
void FTSensor::openSocket(int& handle,const std::string ip,const uint16_t port,const int option)
{
  // create the socket
    if (handle != -1)
        rt_dev_close(handle);
    
    if(option == IPPROTO_UDP)
        handle = rt_dev_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    else if(option == IPPROTO_TCP)
        handle = rt_dev_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    else
        handle = rt_dev_socket(AF_INET, SOCK_DGRAM, 0);
    
    if (handle < 0) {
        std::cerr << "failed to init sensor socket, please make sure your can ping the sensor"<<std::endl;
        return;
    }
    
    // re-use address in case it's still binded
    rt_dev_setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, 0, 0);

    // set the socket parameters
    struct sockaddr_in addr = {0};
    hostent * hePtr = NULL; 
    hePtr = gethostbyname(ip.c_str());
    memcpy(&addr.sin_addr, hePtr->h_addr_list[0], hePtr->h_length);
    
    //addr_.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // connect
    if (rt_dev_connect(handle, (struct sockaddr*) &addr, sizeof(addr)) < 0)
        std::cerr  << "\033[1;31mCould not connect to "<<ip<<":"<<port<<"\033[0m" << std::endl ;
    return;
}
bool FTSensor::closeSockets()
{
  return closeSocket(socketHandle_) > 0 && closeSocket(socketHTTPHandle_) > 0;
}
int FTSensor::closeSocket(const int& handle)
{
  if(handle < 0 )
      return true;
  return rt_dev_close(handle);
}
bool FTSensor::getCalibrationData()
{
  std::string index("");
  if(calibration_index != ati::current_calibration)
  {
    std::stringstream ss;
    ss << calibration_index;
    index = "?index=" + ss.str();
    std::cout << "Using calibration index "<<calibration_index<< std::endl;
  }else
      std::cout << "Using current calibration" << std::endl;
  
#ifndef HAVE_RTNET
  xmlNode *root_element = NULL;
  std::string filename = "http://"+getIP()+"/netftapi2.xml"+index;

  xmlDocPtr doc = xmlReadFile(filename.c_str(), NULL, 0);
  if (doc != NULL)
  {
      root_element = xmlDocGetRootElement(doc);
      
      std::string cfgcpf;
      findElementRecusive(root_element,"cfgcpf",cfgcpf);
      resp_.cpf = static_cast<uint32_t>(::atoi(cfgcpf.c_str()));
      
      std::string cfgcpt;
      findElementRecusive(root_element,"cfgcpt",cfgcpt);
      resp_.cpt = static_cast<uint32_t>(::atoi(cfgcpt.c_str()));
      
      std::string setbias;
      findElementRecusive(root_element,"setbias",setbias);
      // 6 tokens separated by semi-colon
      if (!getArrayFromString<int>(setbias,';',setbias_, 6))
      {
        std::cerr << "Could not get gauge bias values"<<std::endl;
      }
      std::cout << "Sucessfully retrieved counts per force : "<<resp_.cpf<<std::endl;
      std::cout << "Sucessfully retrieved counts per torque : "<<resp_.cpt<<std::endl;
      /* std::cout << "current gauge bias "<< setbias_[0] <<", "
                                        << setbias_[1] <<", "
                                        << setbias_[2] <<", "
                                        << setbias_[3] <<", "
                                        << setbias_[4] <<", "
                                        << setbias_[5] <<", " <<std::endl;
      */
      
      // Read the RDT Output rate
      //xmlChar cfgcomrdtrate[40];
      std::string cfgcomrdtrate;
      findElementRecusive(root_element,"comrdtrate",cfgcomrdtrate);
      std::stringstream cfgcomrdtrate_ss;
      cfgcomrdtrate_ss << cfgcomrdtrate;
      cfgcomrdtrate_ss >> rdt_rate_;
      
      xmlFreeDoc(doc);
      xmlCleanupParser();
              
      return true;
  }
  xmlCleanupParser();
#else
    static const uint32_t chunkSize = 4;        // Every chunk of data will be of this size
    static const uint32_t maxSize = 65536;      // The maximum file size to receive
                          // The recv buffer
    std::string filename = "/netftapi2.xml"+index; // the name of the file to reveice
    std::string host = getIP(); 
    
    std::string request_s = "GET "+filename+" HTTP/1.1\r\nHost: "+host+"\r\n\r\n";

    if (rt_dev_send(socketHTTPHandle_, request_s.c_str(),request_s.length(), 0) < 0)
        std::cerr << "Could not send GET request to "<<getIP()<<":80. Please make sure that RTnet TCP protocol is installed"<<std::endl;
    
    int recvLength=0;
    int posBuff = 0;
    while(posBuff < maxSize) // Just a security to avoid infinity loop
    {
            recvLength = rt_dev_recv(socketHTTPHandle_, &xml_c_[posBuff],chunkSize, 0);
            posBuff += recvLength;
            if(recvLength <= 0) // The last chunk returns 0
                break;
    }
    xml_s_ = xml_c_;

    const uint32_t cfgcpf_r = getNumberInXml<uint32_t>(xml_s_,"cfgcpf");
    const uint32_t cfgcpt_r = getNumberInXml<uint32_t>(xml_s_,"cfgcpt");
    const int cfgcomrdtrate = getNumberInXml<int>(xml_s_,"comrdtrate");
    rdt_rate_ = cfgcomrdtrate;
    
    // 6 tokens separated by semi-colon
    if (!getArrayFromXml<int>(xml_s_,"setbias",';',setbias_, 6))
    {
        std::cerr << "Could not get gauge bias values"<<std::endl;
    }

    if(cfgcpf_r && cfgcpt_r)
    {
        resp_.cpf = cfgcpf_r;
        resp_.cpt = cfgcpt_r;
        std::cout << "Sucessfully retrieved counts per force : "<<resp_.cpf<<std::endl;
        std::cout << "Sucessfully retrieved counts per torque : "<<resp_.cpt<<std::endl;
        return true;
    }
#endif
  std::cerr << "Could not parse file " << filename<<std::endl;
  std::cerr << "Using default counts per force : "<<resp_.cpf<<std::endl;
  std::cerr << "Using default counts per torque : "<<resp_.cpt<<std::endl;
  return false;
}

bool FTSensor::sendTCPrequest(std::string &request_cmd)
{
  if (request_cmd.empty() )
  {
    std::cerr << "Empty TCP command, not sending"<<std::endl;
    return false;
  }
  else
  {
    static const uint32_t chunkSize = 4;        // Every chunk of data will be of this size
    static const uint32_t maxSize = 65536;      // The maximum file size to receive
    std::string host = getIP();

    std::string request_s = "GET "+request_cmd+" HTTP/1.0\r\nHost: "+host+"\r\n\r\n";

    if (rt_dev_send(socketHTTPHandle_, request_s.c_str(),request_s.length(), 0) < 0)
    {
  #ifndef HAVE_RTNET
        std::cerr << "Could not send GET request to "<<host<<":80."<<std::endl;
  #else
        std::cerr << "Could not send GET request to "<<host<<":80. Please make sure that RTnet TCP protocol is installed"<<std::endl;
  #endif
        return false;
    }
    
    //empty the buffer but we don't care about the result
    int recvLength=0;
    int posBuff = 0;
    while(posBuff < maxSize) // Just a security to avoid infinity loop
    {
        recvLength = rt_dev_recv(socketHTTPHandle_, &xml_c_[posBuff],chunkSize, 0);
        posBuff += recvLength;
        if(recvLength <= 0) // The last chunk returns 0
            break;
    }
    if (posBuff > 4)
    {
        const char *awaited_response = "HTTP/1.0 302 Found";
        if (strncmp(xml_c_, awaited_response, 18 )==0)
        {
            return true;
        }
        else
        {
            std::cerr << "Bad response from set command. Response is :" <<  xml_c_ << std::endl;
            return false;
        }
    }
    else
    {
        std::cerr << "Bad response from set command. Response is :" <<  xml_c_  << std::endl;
        return false;
    }
  }
}


bool FTSensor::setRDTOutputRate(unsigned int rate)
{
  if (rate > 0 && rate <= 7000)
  {
      std::stringstream cfgcomrdtrate_ss;
      cfgcomrdtrate_ss << rate;
      std::string cmd = "/comm.cgi?comrdtrate=" + cfgcomrdtrate_ss.str();
      
      if(sendTCPrequest(cmd))
      { 
          // we consider the rate was set and don't read it back
          rdt_rate_ = rate;
          return true;
      }
      else
          return false;
  }
  else
  {
      std::cerr << "RDT rate must be in range [1-7000]" << std::endl;
      return false;
  }
}


bool FTSensor::setGaugeBias(unsigned int gauge_idx, int gauge_bias)
{
  std::map<unsigned int, int> map;
  map[gauge_idx] = gauge_bias;
  return setGaugeBias(map);
}

std::vector<int> FTSensor::getGaugeBias()
{
  std::vector<int> bias(setbias_, setbias_ + 6);
  return bias;
}


bool FTSensor::setGaugeBias(std::vector<int> &gauge_vect)
{
  std::map<unsigned int, int> map;
  for (size_t i=0; i < gauge_vect.size(); ++i)
  {
    map[i] = gauge_vect[i];
  }
  return setGaugeBias(map);
}

bool FTSensor::setGaugeBias(std::map<unsigned int, int> &gauge_map)
{
  std::stringstream setbias_ss;
  std::map<unsigned int, int>::iterator it;
  bool first_element = true;
  //prepare the query
  for (it=gauge_map.begin(); it!=gauge_map.end(); ++it)
  {
    if( it->first < 6)
    {
      if(first_element)
      {
        setbias_ss << "?";
        first_element = false;
      }
      else
      {
        setbias_ss << "&";
      }
      setbias_ss << "setbias" << it->first << "=" << it->second;
    }
    else
    {
      std::cerr << "Invalid gauge number "<< it->first << std::endl;
      return false;
    }
  }

  std::string host = getIP(); 
  std::string cmd = "/setting.cgi" + setbias_ss.str();
  return sendTCPrequest(cmd);
}

bool FTSensor::sendCommand()
{
  return sendCommand(cmd_.command); 
}

bool FTSensor::sendCommand(uint16_t cmd)
{
  *reinterpret_cast<uint16_t*>(&request_[0]) = htons(command_s::command_header);
  *reinterpret_cast<uint16_t*>(&request_[2]) = htons(cmd); 
  *reinterpret_cast<uint32_t*>(&request_[4]) = htonl(cmd_.sample_count);
  //return rt_dev_sendto(socketHandle_, (void*) &request_, sizeof(request_), 0, (sockaddr*) &addr_, addr_len_ ) == 8;
  return rt_dev_send(socketHandle_, (void*) &request_, sizeof(request_), 0) == sizeof(request_);//, (sockaddr*) &addr_, addr_len_ ) == 8;
}

bool FTSensor::getResponse()
{

  //response_ret_ = rt_dev_recvfrom(socketHandle_, (void*) &response_, sizeof(response_), 0, (sockaddr*) &addr_, &addr_len_ );
  response_ret_ = rt_dev_recv(socketHandle_, (void*) &response_, sizeof(response_), 0);//, (sockaddr*) &addr_, &addr_len_ );
  resp_.rdt_sequence = ntohl(*reinterpret_cast<uint32_t*>(&response_[0]));
  resp_.ft_sequence = ntohl(*reinterpret_cast<uint32_t*>(&response_[4]));
  resp_.status = ntohl(*reinterpret_cast<uint32_t*>(&response_[8]));
  resp_.Fx = static_cast<int32_t>(ntohl(*reinterpret_cast<int32_t*>(&response_[12 + 0 * 4])));
  resp_.Fy = static_cast<int32_t>(ntohl(*reinterpret_cast<int32_t*>(&response_[12 + 1 * 4])));
  resp_.Fz = static_cast<int32_t>(ntohl(*reinterpret_cast<int32_t*>(&response_[12 + 2 * 4])));
  resp_.Tx = static_cast<int32_t>(ntohl(*reinterpret_cast<int32_t*>(&response_[12 + 3 * 4])));
  resp_.Ty = static_cast<int32_t>(ntohl(*reinterpret_cast<int32_t*>(&response_[12 + 4 * 4])));
  resp_.Tz = static_cast<int32_t>(ntohl(*reinterpret_cast<int32_t*>(&response_[12 + 5 * 4])));
  if (response_ret_ < 0)
  {
    std::cerr << "Error while receiving: " << strerror(errno) << std::endl;
  }
  if (response_ret_!=RDT_RECORD_SIZE)
    std::cerr << "Error of package size " <<response_ret_ << " but should be "<< RDT_RECORD_SIZE <<std::endl;
  return response_ret_==RDT_RECORD_SIZE;
}

void FTSensor::doComm()
{
    if (isInitialized()) {
        if(cmd_.sample_count != 0) //do not repeat send if infinite samples
            if(!sendCommand())
                std::cerr << "Error while sending command" << std::endl;
        if(!getResponse())
            std::cerr << "Error while getting response, command:" <<cmd_.command <<std::endl;
    }
}


void FTSensor::setBias()
{
  //std::cout << "Setting bias"<<std::endl;
  this->setSoftwareBias();
}
bool FTSensor::isInitialized()
{
    return initialized_;
}

void FTSensor::setTimeout(float sec)
{
    if (sec <= 0) {
        std::cerr << "Can't set timeout <= 0 sec" << std::endl;
        return;
    }
        
    if (isInitialized()) {
        std::cerr << "Can't set timeout if socket is initialized, call this before init()." << std::endl;
        return;
    }
  timeval_.tv_sec = static_cast<unsigned int>(sec);
  timeval_.tv_usec = static_cast<unsigned int>(sec/1.e6);
}

bool FTSensor::resetThresholdLatch()
{
  if(! sendCommand(command_s::RESET_THRESHOLD_LATCH)){
    std::cerr << "Could not start reset threshold latch" << std::endl;
      return false;
  }
  return true;
}
bool FTSensor::setSoftwareBias()
{
  //if(!stopStreaming())
      //std::cerr << "Could not stop streaming" << std::endl;
  if(! sendCommand(command_s::SET_SOFWARE_BIAS)){
    ;//std::cerr << "Could not set software bias" << std::endl;
      return false;
  }
  //if(!startStreaming())
      //std::cerr << "Could not restart streaming" << std::endl;
  return true;
}
bool FTSensor::stopStreaming()
{
  return sendCommand(command_s::STOP);
}

bool FTSensor::startBufferedStreaming(uint32_t sample_count)
{
  setSampleCount(sample_count);
  setCommand(command_s::BUFFERED);
  if(! sendCommand()){
    std::cerr << "Could not start buffered streaming" << std::endl;
      return false;
  }
  return true;
}
bool FTSensor::startMultiUnitStreaming(uint32_t sample_count)
{
  setSampleCount(sample_count);
  setCommand(command_s::MULTIUNIT);
  if(! sendCommand()){
    std::cerr << "Could not start multi-unit streaming" << std::endl;
      return false;
  }
  return true;
}
bool FTSensor::startRealTimeStreaming(uint32_t sample_count)
{
  setSampleCount(sample_count);
  setCommand(command_s::REALTIME);
  if(! sendCommand()){
    std::cerr << "Could not start realtime streaming" << std::endl;
    return false;
  }
  std::cout << "Start realtime streaming with " << sample_count <<" samples " << std::endl;
  return true;
}

void FTSensor::setCommand(uint16_t cmd)
{
  this->cmd_.command = cmd;
}

void FTSensor::setSampleCount(uint32_t sample_count)
{
  this->cmd_.sample_count = sample_count;
}

