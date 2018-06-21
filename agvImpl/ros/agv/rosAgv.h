#ifndef ROSAGV_H
#define ROSAGV_H

#include "agv.h"
#include "agvtask.h"
#include "qyhtcpclient.h"
#include "userlogmanager.h"
#include "msgprocess.h"
#include "linepath.h"
#include "device/device.h"
#include "qunchuang/chipmounter/chipmounter.h"

using namespace std;

#define SIMULATOR 1

#define NAV_CTRL_USING_TOPIC  0

#if SIMULATOR
#define AGV_POSE_TOPIC_NAME  "/base_pose_ground_truth"
#define AGV_POSE_TOPIC_TYPE  "nav_msgs/Odometry"
#else
#define AGV_POSE_TOPIC_NAME  "/robot_pose"
#define AGV_POSE_TOPIC_TYPE  "geometry_msgs/Pose"
#endif



class rosAgv;
using rosAgvPtr = std::shared_ptr<rosAgv>;

//如下 NAV_CONTROL define 是在 AGV中定义, 不能随便更改
enum nav_control{
    STOP,
    START,
    PAUSE
};

#define NAV_CTRL_STATUS_ERROR      -1
#define NAV_CTRL_STATUS_IDLING     0
#define NAV_CTRL_STATUS_RUNNING    1
#define NAV_CTRL_STATUS_PAUSED     2
#define NAV_CTRL_STATUS_COMPLETED  3
#define NAV_CTRL_STATUS_CANCELLED  4
#define NAV_CTRL_STATUS_SUB_CANCELLED  5
//如上 NAV_CONTROL define 是在 AGV中定义, 不能随便更改

//AGV 货架转动方向
#define AGV_SHELVES_ROLLING_FORWORD true  // 向前转， AGV方向
#define AGV_SHELVES_ROLLING_BACKWORD false // 向后转

class rosAgv : public Agv
{
private:
       //geometry_msgs::Pose agvPose;
       std::mutex parseDataMtx;

       std::mutex nav_ctrl_status_mutex;
       std::mutex shelf_status_mutex;

       std::condition_variable nav_ctrl_status_var; // nav_ctrl_status条件变量.

       void subTopic(const char * topic, const char * topic_type);
       void advertiseTopic(const char * topic, const char * topic_type);
       void advertiseService(const char * service_name, const char * msg_type);

       void publishTopic(const char * topic, Json::Value msg);
       void parseJsondata(const char *data,int len);
       void processServiceCall(Json::Value call_service);
       void processServiceResponse(Json::Value response);
       void sendServiceResponse(string service_name,Json::Value *value=nullptr,string id="");
       void navCtrlStatusNotify(string waypoint_name, int nav_ctrl_status);
       void changeMap(string map_name);
       void startRolling(bool forword);//send to 偏贴机AGV start rolling topic
       void stopRolling();//send to 偏贴机AGV stop rolling topic

       void startShelftUp(string action);
       void startShelftDown(string action);
       void initStation(string station_name);



public:
    rosAgv(int id,std::string name,std::string ip,int port);

    enum { Type = Agv::Type+1 };

    int type(){return Type;}

    void onRead(const char *data,int len);
    void onConnect();
    void onDisconnect();

    bool startTask(std::string task_name);
    virtual void cancelTask();
    //virtual void excutePath(std::vector<AgvLinePtr> lines);
    virtual void excutePath(std::vector<int> lines);
    void test();
    void test2();

    void startTask(string station, string action);


    bool beforeDoing(string ip, int port, string action, int station_id);
    bool Doing(string action, int station_id);
    bool afterDoing(string action, int station_id);
    void setChipMounter(chipmounter* device);// only for test, will be removed

    bool isAGVInit();


private:
    virtual void arrve(int x,int y);
    //void goStation(AgvStationPtr station, bool stop = false, std::vector<AgvLinePtr> lines);
    //void goStation(AgvStationPtr station, AgvLinePtr line, bool stop = false);
    //void goStation(AgvStationPtr station, std::vector<AgvLinePtr> lines,  bool stop);
    //void goStation(int station, AgvLinePtr line, bool stop = false);
    void goStation(std::vector<int> lines,  bool stop);
    //virtual void goStation(int station, bool stop = false);
    virtual void stop();
    virtual void callMapChange(int station);
    void setAgvPath(std::vector<Pose2D> path);
    bool sendJsonToAGV(Json::Value json);

    //nav ctrl status 状态
    int m_nav_ctrl_status;

    chipmounter* mChipmounter; //偏贴机

    bool m_bInitlayer;
    int  m_agv_type;

    void InitShelfLayer();
    void ControlShelfUpDown(int layer, string height);

};

#endif // ROSAGV_H