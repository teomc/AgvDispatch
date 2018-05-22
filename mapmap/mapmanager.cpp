﻿#include "mapmanager.h"
#include "../sqlite3/CppSQLite3.h"
#include "../msgprocess.h"
#include "../common.h"
#include "../taskmanager.h"
#include "../userlogmanager.h"
#include "../base64.h"
#include <algorithm>

MapManager::MapManager() :mapModifying(false)
{
}

void MapManager::checkTable()
{
    //检查表
    try {
        if (!g_db.tableExists("agv_station")) {
            g_db.execDML("create table agv_station(id INTEGER,name char(64),type INTEGER, x INTEGER,y INTEGER,realX INTEGER,realY INTEGER,labelXoffset INTEGER,labelYoffset INTEGER,mapChange BOOL,locked BOOL);");
        }
        if (!g_db.tableExists("agv_line")) {
            g_db.execDML("create table agv_line(id INTEGER,name char(64),type INTEGER,start INTEGER,end INTEGER,p1x INTEGER,p1y INTEGER,p2x INTEGER,p2y INTEGER,length INTEGER,locked BOOL);");
        }
        if (!g_db.tableExists("agv_bkg")) {
            g_db.execDML("create table agv_bkg(id INTEGER,name char(64),data blob,data_len INTEGER,x INTEGER,y INTEGER,width INTEGER,height INTEGER,filename char(512));");
        }
        if (!g_db.tableExists("agv_floor")) {
            g_db.execDML("create table agv_floor(id INTEGER,name char(64),point INTEGER,path INTEGER,bkg INTEGER);");
        }
        if (!g_db.tableExists("agv_block")) {
            g_db.execDML("create table agv_block(id INTEGER,name char(64),spirit INTEGER);");
        }
        if (!g_db.tableExists("agv_group")) {
            g_db.execDML("create table agv_group(id INTEGER,name char(64),spirit INTEGER,agv INTEGER);");
        }
    }
    catch (CppSQLite3Exception &e) {
        combined_logger->error("sqlerr code:{0} msg:{1}",e.errorCode(), e.errorMessage());
        return;
    }
    catch (std::exception e) {
        combined_logger->error("sqlerr code:{0}",e.what());
        return;
    }
}

//载入地图
bool MapManager::load()
{
    checkTable();
    //载入数据
    mapModifying = true;
    if (!loadFromDb())
    {
        mapModifying = false;
        return false;
    }

    mapModifying = false;
    return true;
}

//保存到数据库
bool MapManager::save()
{
    try {
        checkTable();

        g_db.execDML("delete from agv_station;");

        g_db.execDML("delete from agv_line;");

        g_db.execDML("delete from agv_bkg;");

        g_db.execDML("delete from agv_floor;");

        g_db.execDML("delete from agv_block;");

        g_db.execDML("delete from agv_group;");

        g_db.execDML("begin transaction;");

        std::list<MapSpirit *> spirits = g_onemap.getAllElement();

        CppSQLite3Buffer bufSQL;

        for(auto spirit:spirits){
            if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Point){
                MapPoint *station = static_cast<MapPoint *>(spirit);
                bufSQL.format("insert into agv_station(id,name,type, x,y,realX,realY,labelXoffset,labelYoffset ,mapChange,locked) values (%d, '%s',%d,%d,%d,%d,%d,%d,%d,%d,%d);",station->getId(),station->getName().c_str(),station->getPointType(),station->getX(),station->getY(),
                              station->getRealX(),station->getRealY(),station->getLabelXoffset(),station->getLabelYoffset(),station->getMapChange(),station->getLocked());
                g_db.execDML(bufSQL);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Path){
                MapPath *path = static_cast<MapPath *>(spirit);
                bufSQL.format("insert into agv_line(id ,name,type ,start ,end ,p1x ,p1y ,p2x ,p2y ,length ,locked) values (%d,'%s', %d,%d, %d, %d, %d, %d, %d, %d, %d);",path->getId(),path->getName().c_str(),path->getPathType(),path->getStart(),path->getEnd(),
                              path->getP1x(),path->getP1y(),path->getP2x(),path->getP2y(),path->getLength(),path->getLocked());
                g_db.execDML(bufSQL);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Background){
                MapBackground *bkg = static_cast<MapBackground *>(spirit);
                CppSQLite3Binary blob;
                blob.setBinary((unsigned char *)bkg->getImgData(), bkg->getImgDataLen());
                bufSQL.format("insert into agv_bkg(id ,name,data,data_len,x ,y ,width ,height ,filename) values (%d,'%s',%Q, %d,%d, %d, %d, %d,'%s');",bkg->getId(),bkg->getName().c_str(),blob.getEncoded(),bkg->getImgDataLen(),bkg->getX(),bkg->getY(),bkg->getWidth(),bkg->getHeight(),bkg->getFileName().c_str());
                g_db.execDML(bufSQL);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Floor){
                MapFloor *floor = static_cast<MapFloor *>(spirit);
                std::stringstream pointstr;
                std::list<int> ps = floor->getPoints();
                for(auto p:ps){
                    pointstr<<p<<";";
                }
                std::stringstream pathstr;
                std::list<int> pas = floor->getPoints();
                for(auto pa:pas){
                    pathstr<<pa<<";";
                }
                bufSQL.format("insert into agv_floor(id ,name,point,path,bkg ) values (%d,'%s', '%s','%s',%d);",floor->getId(),floor->getName().c_str(),pointstr.str().c_str(),pathstr.str().c_str(),floor->getBkg());
                g_db.execDML(bufSQL);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Block){
                MapBlock *block = static_cast<MapBlock *>(spirit);
                std::stringstream str;
                std::list<int> ps = block->getSpirits();
                for(auto p:ps)str<<p<<";";
                bufSQL.format("insert into agv_block(id ,name,spirit) values (%d,'%s', '%s');",block->getId(),block->getName().c_str(),str.str().c_str());
                g_db.execDML(bufSQL);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Group){
                MapGroup *group = static_cast<MapGroup *>(spirit);

                std::stringstream str1;
                std::list<int> ps1 = group->getSpirits();
                for(auto p:ps1)str1<<p<<";";

                std::stringstream str2;
                std::list<int> ps2 = group->getAgvs();
                for(auto p:ps2)str2<<p<<";";

                bufSQL.format("insert into agv_group(id ,name,spirit,agv) values (%d,'%s', '%s', '%s');",group->getId(),group->getName().c_str(),str1.str().c_str(),str2.str().c_str());
                g_db.execDML(bufSQL);
            }
        }

        g_db.execDML("commit transaction;");
    }
    catch (CppSQLite3Exception &e) {
        combined_logger->error("sqlerr code:{0} msg:{1}",e.errorCode(), e.errorMessage());
        return false;
    }
    catch (std::exception e) {
        combined_logger->error("sqlerr code:{0}",e.what());
        return false;
    }
    return true;
}

//从数据库中载入地图
bool MapManager::loadFromDb()
{
    try {
        checkTable();

        CppSQLite3Table table_station = g_db.getTable("select id, name, x ,y ,type ,realX ,realY ,labelXoffset ,labelYoffset ,mapChange ,locked  from agv_station;");
        if (table_station.numRows() > 0 && table_station.numFields() != 11)return false;
        for (int row = 0; row < table_station.numRows(); row++)
        {
            table_station.setRow(row);

            int id = atoi(table_station.fieldValue(0));
            std::string name = std::string(table_station.fieldValue(1));
            int x = atoi(table_station.fieldValue(2));
            int y = atoi(table_station.fieldValue(3));
            int type = atoi(table_station.fieldValue(4));
            int realX = atoi(table_station.fieldValue(5));
            int realY = atoi(table_station.fieldValue(6));
            int labeXoffset = atoi(table_station.fieldValue(7));
            int labelYoffset = atoi(table_station.fieldValue(8));
            bool mapChanged = atoi(table_station.fieldValue(9)) == 1;
            bool locked = atoi(table_station.fieldValue(10)) == 1;

            MapPoint *point = new MapPoint(id, name, (MapPoint::Map_Point_Type)type, x, y, realX, realY, labeXoffset, labelYoffset, mapChanged, locked);

            g_onemap.addSpirit(point);
        }

        CppSQLite3Table table_line = g_db.getTable("select id,name,type,start,end,p1x,p1y,p2x,p2y,length,locked from agv_line;");
        if (table_line.numRows() > 0 && table_line.numFields() != 11)return false;
        for (int row = 0; row < table_line.numRows(); row++)
        {
            table_line.setRow(row);

            int id = atoi(table_line.fieldValue(0));
            std::string name = std::string(table_line.fieldValue(1));
            int type = atoi(table_line.fieldValue(2));
            int start = atoi(table_line.fieldValue(3));
            int end = atoi(table_line.fieldValue(4));
            int p1x = atoi(table_line.fieldValue(5));
            int p1y = atoi(table_line.fieldValue(6));
            int p2x = atoi(table_line.fieldValue(7));
            int p2y = atoi(table_line.fieldValue(8));
            int length = atoi(table_line.fieldValue(9));
            bool locked = atoi(table_line.fieldValue(10)) == 1;

            MapPath *path = new MapPath(id, name, start, end, (MapPath::Map_Path_Type)type, length, p1x, p1y, p2x, p2y, locked);
            g_onemap.addSpirit(path);
        }


        CppSQLite3Table table_bkg = g_db.getTable("select id,name,data,data_len,x,y,width,height,filename from agv_bkg;");
        if (table_bkg.numRows() > 0 && table_bkg.numFields() != 9)return false;
        for (int row = 0; row < table_bkg.numRows(); row++)
        {
            table_bkg.setRow(row);

            int id = atoi(table_bkg.fieldValue(0));
            std::string name = std::string(table_bkg.fieldValue(1));

            CppSQLite3Binary blob;
            blob.setEncoded((unsigned char*)table_bkg.fieldValue(2));
            char *data = new char[blob.getBinaryLength()];
            memcpy(data,blob.getBinary(),blob.getBinaryLength());
            int data_len = atoi(table_bkg.fieldValue(3));
            int x = atoi(table_bkg.fieldValue(4));
            int y = atoi(table_bkg.fieldValue(5));
            int width = atoi(table_bkg.fieldValue(6));
            int height = atoi(table_bkg.fieldValue(7));
            std::string filename = std::string(table_bkg.fieldValue(8));
            MapBackground *bkg = new MapBackground(id,name,data,data_len,width,height,filename);
            bkg->setX(x);
            bkg->setY(y);
            g_onemap.addSpirit(bkg);
        }


        CppSQLite3Table table_floor = g_db.getTable("select id,name,point,path,bkg from agv_floor;");
        if (table_floor.numRows() > 0 && table_floor.numFields() != 5)return false;
        for (int row = 0; row < table_floor.numRows(); row++)
        {
            table_floor.setRow(row);

            int id = atoi(table_floor.fieldValue(0));
            std::string name = std::string(table_floor.fieldValue(1));

            MapFloor *mfloor = new MapFloor(id,name);

            std::string pointstr = std::string(table_floor.fieldValue(2));
            std::string pathstr = std::string(table_floor.fieldValue(3));
            int bkg = atoi(table_floor.fieldValue(4));

            std::vector<std::string> pvs = split(pointstr,";");
            for(auto p:pvs){
                int intp;
                std::stringstream ss;
                ss<<p;
                ss>>intp;
                mfloor->addPoint(intp);
            }

            std::vector<std::string> avs = split(pathstr,";");
            for(auto p:avs){
                int intp;
                std::stringstream ss;
                ss<<p;
                ss>>intp;
                mfloor->addPath(intp);
            }

            mfloor->setBkg(bkg);
            g_onemap.addSpirit(mfloor);
        }


        CppSQLite3Table table_block = g_db.getTable("select id,name,spirit from agv_block;");
        if (table_block.numRows() > 0 && table_block.numFields() != 3)return false;
        for (int row = 0; row < table_block.numRows(); row++)
        {
            table_block.setRow(row);

            int id = atoi(table_block.fieldValue(0));
            std::string name = std::string(table_block.fieldValue(1));

            MapBlock *mblock = new MapBlock(id,name);

            std::string pointstr = std::string(table_block.fieldValue(2));
            std::vector<std::string> pvs = split(pointstr,";");
            for(auto p:pvs){
                int intp;
                std::stringstream ss;
                ss<<p;
                ss>>intp;
                mblock->addSpirit(intp);
            }

            g_onemap.addSpirit(mblock);
        }

        CppSQLite3Table table_group = g_db.getTable("select id,name,spirit,agv from agv_group;");
        if (table_group.numRows() > 0 && table_group.numFields() != 4)return false;
        for (int row = 0; row < table_group.numRows(); row++)
        {
            table_group.setRow(row);

            int id = atoi(table_group.fieldValue(0));
            std::string name = std::string(table_group.fieldValue(1));

            MapGroup *mgroup = new MapGroup(id,name);

            std::string pointstr = std::string(table_group.fieldValue(2));
            std::string pathstr = std::string(table_group.fieldValue(3));

            std::vector<std::string> pvs = split(pointstr,";");
            for(auto p:pvs){
                int intp;
                std::stringstream ss;
                ss<<p;
                ss>>intp;
                mgroup->addSpirit(intp);
            }

            std::vector<std::string> avs = split(pathstr,";");
            for(auto p:avs){
                int intp;
                std::stringstream ss;
                ss<<p;
                ss>>intp;
                mgroup->addAgv(intp);
            }
            g_onemap.addSpirit(mgroup);
        }

        int max_id = 0;
        auto ps = g_onemap.getAllElement();
        for(auto p:ps){
            if(p->getId()>max_id)max_id = p->getId();
        }
        g_onemap.setMaxId(max_id);
        getReverseLines();
        getAdj();
    }
    catch (CppSQLite3Exception &e) {
        combined_logger->error("sqlerr code:{0} msg:{1}",e.errorCode(), e.errorMessage());
        return false;
    }
    catch (std::exception e) {
        combined_logger->error("sqlerr code:{0}",e.what());
        return false;
    }
    return true;
}


//获取最优路径
std::vector<int> MapManager::getBestPath(int agv, int lastStation, int startStation, int endStation, int &distance, bool changeDirect)
{
    distance = DISTANCE_INFINITY;
    if (mapModifying) return std::vector<int>();
    int disA = DISTANCE_INFINITY;
    int disB = DISTANCE_INFINITY;

    std::vector<int> a = getPath(agv, lastStation, startStation, endStation, disA, false);
    std::vector<int> b;
    if (changeDirect) {
        b = getPath(agv, startStation, lastStation, endStation, disB, true);
        if (disA != DISTANCE_INFINITY && disB != DISTANCE_INFINITY) {
            distance = disA < disB ? disA : disB;
            if (disA < disB)return a;
            return b;
        }
    }
    if (disA != DISTANCE_INFINITY) {
        distance = disA;
        return a;
    }
    distance = disB;
    return b;
}

std::vector<int> MapManager::getPath(int agv, int lastStation, int startStation, int endStation, int &distance, bool changeDirect)
{
    std::vector<int> result;
    distance = DISTANCE_INFINITY;

    auto paths = g_onemap.getPaths();

    //判断station是否正确
    if (lastStation <= 0) lastStation = startStation;
    if (lastStation <= 0)return result;
    if (startStation <= 0)return result;
    if (endStation <= 0)return result;

    auto lastStationPtr =  g_onemap.getSpiritById(lastStation);
    auto startStationPtr =  g_onemap.getSpiritById(startStation);
    auto endStationPtr =  g_onemap.getSpiritById(endStation);

    if(lastStationPtr == nullptr || startStationPtr == nullptr || endStationPtr == nullptr)return result;


    if(lastStationPtr->getSpiritType()!=MapSpirit::Map_Sprite_Type_Point
            ||startStationPtr->getSpiritType()!=MapSpirit::Map_Sprite_Type_Point
            ||endStationPtr->getSpiritType()!=MapSpirit::Map_Sprite_Type_Point)
        return result;

    //判断站点占用清空
    if(station_occuagv[startStation]!=0 && station_occuagv[startStation]!=agv)return result;
    if(station_occuagv[endStation]!=0 && station_occuagv[endStation]!=agv)return result;

    //group的判断
    bool agvCanGo = false;
    std::list<MapGroup *> groups = g_onemap.getGroups();
    for(auto group:groups){
        auto spirits = group->getSpirits();
        auto agvs = group->getAgvs();
        if(std::find(spirits.begin(),spirits.end(),endStation)!=spirits.end()
                && std::find(agvs.begin(),agvs.end(),agv)!=agvs.end()){
            agvCanGo = true;
            break;
        }
    }

    if(startStation == endStation){
        distance = 0;
        if (changeDirect && lastStation != startStation) {
            for(auto path:paths){
                if(path->getStart() == lastStation && path->getEnd() == startStation){
                    result.push_back(path->getId());
                    distance=path->getLength();
                }
            }
        }
        return result;
    }

    std::multimap<int,int> Q;// distance -- lineid

    struct LineDijkInfo{
        int father = 0;
        int distance = DISTANCE_INFINITY;
        int color = AGV_LINE_COLOR_WHITE;
    };
    std::map<int,LineDijkInfo> lineDistanceColors;

    //初始化，所有线路 距离为无限远、color为尚未标记
    for(auto path:paths){
        lineDistanceColors[path->getId()].father = 0;
        lineDistanceColors[path->getId()].distance = DISTANCE_INFINITY;
        lineDistanceColors[path->getId()].color = AGV_LINE_COLOR_WHITE;
    }


    ////增加一种通行的判定：
    ////如果AGV2 要从 C点 到达 D点，同事路过B点。
    ////同时AGV1 要从 A点 到达 B点。如果AGV1先到达B点，会导致AGV2 无法继续运行。
    ////判定终点的线路 是否占用
    ////endPoint是终点
    {
        for (auto templine : paths) {
            if (templine->getStart() == endStation) {
                if(line_occuagvs[templine->getId()].size()>1 || (line_occuagvs[templine->getId()].size()==1 && *(line_occuagvs[templine->getId()].begin()) != agv)){
                    //TODO:该方式到达这个地方 不可行.该线路 置黑、
                    lineDistanceColors[templine->getId()].color = AGV_LINE_COLOR_BLACK;
                    lineDistanceColors[templine->getId()].distance = DISTANCE_INFINITY;
                }
            }
        }
    }

    if (lastStation == startStation) {
        for (auto line: paths) {
            if (line->getStart() == lastStation) {
                int reverse = m_reverseLines[line->getId()];
                if(reverse>0){
                    //反向线路未被占用//path的终点未被占用//则这条线路可以过去
                    if(line_occuagvs[reverse].size() == 0 &&station_occuagv[line->getEnd()] ==0 || station_occuagv[line->getEnd()] == agv){
                        if(lineDistanceColors[line->getId()].color == AGV_LINE_COLOR_BLACK)continue;
                        lineDistanceColors[line->getId()].distance = line->getLength();
                        lineDistanceColors[line->getId()].color = AGV_LINE_COLOR_GRAY;
                        Q.insert(std::make_pair(lineDistanceColors[line->getId()].distance, line->getId()));
                    }

                }
            }
        }
    }
    else {
        for (auto line : paths) {
            if (line->getStart() == lastStation && line->getEnd() == lastStation) {
                int reverse = m_reverseLines[line->getId()];
                if(reverse>0){
                    if(line_occuagvs[reverse].size() == 0 &&station_occuagv[line->getEnd()] ==0 || station_occuagv[line->getEnd()] == agv){
                        if(lineDistanceColors[line->getId()].color == AGV_LINE_COLOR_BLACK)continue;
                        lineDistanceColors[line->getId()].distance = line->getLength();
                        lineDistanceColors[line->getId()].color = AGV_LINE_COLOR_GRAY;
                        Q.insert(std::make_pair(lineDistanceColors[line->getId()].distance, line->getId()));
                        break;
                    }
                }
            }
        }
    }

    while(Q.size()>0){
        auto front = Q.begin();
        int startLine = front->second;

        std::vector<int> adjs = m_adj[startLine];
        for(auto adj:adjs)
        {
            if(lineDistanceColors[adj].color == AGV_LINE_COLOR_BLACK)continue;

            MapSpirit *pp = g_onemap.getSpiritById(adj);
            if(pp->getSpiritType() != MapSpirit::Map_Sprite_Type_Path)continue;
            MapPath *path = static_cast<MapPath *>(pp);

            if (lineDistanceColors[adj].color == AGV_LINE_COLOR_WHITE) {
                int  reverse = m_reverseLines[adj];
                if(line_occuagvs[reverse].size() == 0 &&station_occuagv[path->getEnd()] ==0 || station_occuagv[path->getEnd()] == agv){
                    lineDistanceColors[adj].distance = lineDistanceColors[startLine].distance + path->getLength();
                    lineDistanceColors[adj].color = AGV_LINE_COLOR_GRAY;
                    lineDistanceColors[adj].father = startLine;
                    Q.insert(std::make_pair(lineDistanceColors[adj].distance,adj));
                }
            }else if(lineDistanceColors[adj].color == AGV_LINE_COLOR_GRAY){
                if(lineDistanceColors[adj].distance > lineDistanceColors[startLine].distance + path->getLength()){
                    //更新father和距离
                    lineDistanceColors[adj].distance = lineDistanceColors[startLine].distance + path->getLength();
                    lineDistanceColors[adj].father = startLine;

                    //更新Q中的 adj

                    //删除旧的
                    for (auto iiitr = Q.begin(); iiitr != Q.end();) {
                        if (iiitr->second == adj) {
                            iiitr = Q.erase(iiitr);
                        }else
                            iiitr++;
                    }
                    //加入新的
                    Q.insert(std::make_pair(lineDistanceColors[adj].distance, adj));

                }
            }
        }
        lineDistanceColors[startLine].color = AGV_LINE_COLOR_BLACK;
        //erase startLine
        for (auto itr = Q.begin(); itr != Q.end();) {
            if (itr->second == startLine) {
                itr = Q.erase(itr);
            }else
                itr++;
        }
    }

    int index = 0;
    int minDis = DISTANCE_INFINITY;

    for (auto ll : paths) {
        if (ll->getEnd() == endStation) {
            if (lineDistanceColors[ll->getId()].distance < minDis) {
                minDis = lineDistanceColors[ll->getId()].distance;
                index = ll->getId();
            }
        }
    }

    distance = minDis;

    while (true) {
        if (index == 0)break;
        result.push_back(index);
        index =  lineDistanceColors[index].father;
    }
    std::reverse(result.begin(), result.end());

    if (result.size() > 0 && lastStation != startStation) {
        if (!changeDirect) {
            int  agv_line = *(result.begin());
            MapSpirit *sp = g_onemap.getSpiritById(agv_line);
            if(sp->getSpiritType() == MapSpirit::Map_Sprite_Type_Path){
                MapPath *path  = static_cast<MapPath *>(sp);
                if(path->getStart() == lastStation && path->getEnd() == startStation){
                    result.erase(result.begin());
                }
            }
        }
    }

    return result;
}


void MapManager::getReverseLines()
{
    std::list<MapPath *> paths = g_onemap.getPaths();

    for(auto a:paths){
        for(auto b:paths){
            if(a==b)continue;
            if(a->getEnd() == b->getStart() && a->getStart() == b->getEnd()){
                m_reverseLines[a->getId()] = b->getId();
            }
        }
    }
}

void MapManager::getAdj()
{

    std::list<MapPath *> paths = g_onemap.getPaths();

    for(auto a:paths){
        for(auto b:paths){
            if(a==b)continue;
            if(a->getEnd() == b->getStart() && a->getStart() != b->getEnd()){
                m_adj[a->getId()].push_back(b->getId());
            }
        }
    }
}

void MapManager::clear()
{
    line_occuagvs.clear();
    station_occuagv.clear();
    block_occuagv.clear();
    m_reverseLines.clear();
    m_adj.clear();
    g_onemap.clear();
}

void MapManager::interSetMap(qyhnetwork::TcpSessionPtr conn, const Json::Value &request)
{
    Json::Value response;
    response["type"] = MSG_TYPE_RESPONSE;
    response["todo"] = request["todo"];
    response["queuenumber"] = request["queuenumber"];
    response["result"] = RETURN_MSG_RESULT_SUCCESS;

    if (TaskManager::getInstance()->hasTaskDoing())
    {
        response["result"] = RETURN_MSG_RESULT_FAIL;
        response["error_code"] = RETURN_MSG_ERROR_CODE_TASKING;
    }
    else {
        UserLogManager::getInstance()->push(conn->getUserName() + " set map");

        mapModifying = true;
        clear();
        try {
            g_db.execDML("delete from agv_station;");
            g_db.execDML("delete from agv_line;");
            g_db.execDML("delete from agv_bkg");
            g_db.execDML("delete from agv_block");
            g_db.execDML("delete from agv_group_group");
            g_db.execDML("delete from agv_group_agv");

            //TODO:
            //1.解析站点
            for (int i = 0; i < request["stations"].size(); ++i)
            {
                Json::Value station = request["stations"][i];
                int id = station["id"].asInt();
                std::string name = station["name"].asString();
                int station_type = station["point_type"].asInt();
                int x = station["x"].asInt();
                int y = station["y"].asInt();
                int realX = station["realX"].asInt();
                int realY = station["realY"].asInt();
                int labelXoffset = station["labelXoffset"].asInt();
                int labelYoffset = station["labelYoffset"].asInt();
                bool mapchanged = station["mapchanged"].asBool();
                bool locked = station["locked"].asBool();
                MapPoint *p = new MapPoint(id,name,(MapPoint::Map_Point_Type)station_type,x,y,realX,realY,labelXoffset,labelYoffset,mapchanged,locked);
                g_onemap.addSpirit(p);
            }

            //2.解析线路
            for (int i = 0; i < request["lines"].size(); ++i)
            {
                Json::Value line = request["lines"][i];
                int id = line["id"].asInt();
                std::string name = line["name"].asString();
                int type = line["type"].asInt();
                int start = line["start"].asInt();
                int end = line["end"].asInt();
                int p1x = line["p1x"].asInt();
                int p1y = line["p1y"].asInt();
                int p2x = line["p2x"].asInt();
                int p2y = line["p2y"].asInt();
                int length = line["length"].asInt();
                bool locked = line["locked"].asBool();
                MapPath *p = new MapPath(id,name,start,end,(MapPath::Map_Path_Type)type,length,p1x,p1y,p2x,p2y,locked);
                g_onemap.addSpirit(p);
            }

            //4.解析背景图片
            for (int i = 0; i < request["bkgs"].size(); ++i)
            {
                Json::Value bkg = request["bkgs"][i];
                int id = bkg["id"].asInt();
                std::string name = bkg["name"].asString();
                std::string database64 = bkg["data"].asString();
                int lenlen = base64_dec_len(database64.c_str(),database64.length());
                char *data = new char[lenlen];
                base64_decode(data,database64.c_str(),database64.length());
                int imgdatalen = bkg["data_len"].asInt();
                int width = bkg["width"].asInt();
                int height = bkg["height"].asInt();
                std::string filename = bkg["filename"].asString();
                MapBackground *p = new MapBackground(id,name,data,imgdatalen,width,height,filename);

                g_onemap.addSpirit(p);
            }

            //3.解析楼层
            for (int i = 0; i < request["floors"].size(); ++i)
            {
                Json::Value floor = request["floors"][i];
                int id = floor["id"].asInt();
                std::string name = floor["name"].asString();
                Json::Value points = floor["points"];
                Json::Value paths = floor["paths"];
                int bkg = floor["bkg"].asInt();
                MapFloor *p = new MapFloor(id,name);
                p->setBkg(bkg);
                for(int k=0;k<points.size();++k){
                    Json::Value point = points[k];
                    p->addPoint(point.asInt());
                }
                for(int k=0;k<paths.size();++k){
                    Json::Value path = points[k];
                    p->addPath(path.asInt());
                }
                g_onemap.addSpirit(p);
            }

            //5.解析block
            for (int i = 0; i < request["blocks"].size(); ++i)
            {
                Json::Value block = request["blocks"][i];
                int id = block["id"].asInt();
                std::string name = block["name"].asString();
                Json::Value spirits = block["spirits"];
                MapBlock *p = new MapBlock(id,name);
                for(int k=0;k<spirits.size();++k){
                    Json::Value spirit = spirits[k];
                    p->addSpirit(spirit.asInt());
                }
                g_onemap.addSpirit(p);
            }

            //6.解析group
            for (int i = 0; i < request["groups"].size(); ++i)
            {
                Json::Value group = request["groups"][i];
                int id = group["id"].asInt();
                std::string name = group["name"].asString();
                Json::Value spirits = group["spirits"];
                MapGroup *p = new MapGroup(id,name);
                for(int k=0;k<spirits.size();++k){
                    Json::Value spirit = spirits[k];
                    p->addSpirit(spirit.asInt());
                }
                Json::Value agvs = group["agvs"];
                for(int k=0;k<agvs.size();++k){
                    Json::Value agv = agvs[k];
                    p->addAgv(agv.asInt());
                }
                g_onemap.addSpirit(p);
            }

            int max_id = request["maxId"].asInt();
            g_onemap.setMaxId(max_id);


            //构建反向线路和adj
            getReverseLines();
            getAdj();

            if(!save()){
                response["result"] = RETURN_MSG_RESULT_FAIL;
                response["error_code"] = RETURN_MSG_ERROR_CODE_SAVE_SQL_FAIL;
                clear();
            }
        }
        catch (CppSQLite3Exception e) {
            response["result"] = RETURN_MSG_RESULT_FAIL;
            response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
            std::stringstream ss;
            ss << "code:" << e.errorCode() << " msg:" << e.errorMessage();
            response["error_info"] = ss.str();
            combined_logger->error("sqlerr code:{0} msg:{1}",e.errorCode(), e.errorMessage());
        }
        catch (std::exception e) {
            response["result"] = RETURN_MSG_RESULT_FAIL;
            response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
            std::stringstream ss;
            ss << "info:" << e.what();
            response["error_info"] = ss.str();
            combined_logger->error("sqlerr code:{0}",e.what());
        }
    }

    mapModifying = false;

    conn->send(response);
}

void MapManager::interGetMap(qyhnetwork::TcpSessionPtr conn, const Json::Value &request)
{
    Json::Value response;
    response["type"] = MSG_TYPE_RESPONSE;
    response["todo"] = request["todo"];
    response["queuenumber"] = request["queuenumber"];
    response["result"] = RETURN_MSG_RESULT_SUCCESS;

    if (mapModifying) {
        response["result"] = RETURN_MSG_RESULT_FAIL;
        response["error_code"] = RETURN_MSG_ERROR_CODE_CTREATING;
    }
    else {
        UserLogManager::getInstance()->push(conn->getUserName() + " get map");

        std::list<MapSpirit *> allspirit = g_onemap.getAllElement();

        Json::Value v_points;
        Json::Value v_paths;
        Json::Value v_floors;
        Json::Value v_bkgs;
        Json::Value v_blocks;
        Json::Value v_groups;
        for(auto spirit:allspirit)
        {
            if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Point){
                MapPoint *p = static_cast<MapPoint *>(spirit);
                Json::Value pv;
                pv["id"] = p->getId();
                pv["name"] = p->getName();
                pv["type"] = p->getPointType();
                pv["x"] = p->getX();
                pv["y"] = p->getY();
                pv["realX"] = p->getRealX();
                pv["realY"] = p->getRealY();
                pv["labelXoffset"] = p->getLabelXoffset();
                pv["labelYoffset"] = p->getLabelYoffset();
                pv["mapChange"] = p->getMapChange();
                pv["locked"] = p->getLocked();
                v_points.append(pv);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Path){
                MapPath *p = static_cast<MapPath *>(spirit);
                Json::Value pv;
                pv["id"] = p->getId();
                pv["name"] = p->getName();
                pv["type"] = p->getPathType();
                pv["start"] = p->getStart();
                pv["end"] = p->getEnd();
                pv["p1x"] = p->getP1x();
                pv["p1y"] = p->getP1y();
                pv["p2x"] = p->getP2x();
                pv["p2y"] = p->getP2y();
                pv["length"] = p->getLength();
                pv["locked"] = p->getLocked();
                v_paths.append(pv);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Background){
                MapBackground *p = static_cast<MapBackground *>(spirit);
                Json::Value pv;
                pv["id"] = p->getId();
                pv["name"] = p->getName();

                int lenlen = base64_enc_len(p->getImgDataLen());
                char *ss = new char[lenlen+1];
                base64_encode(ss,p->getImgData(),p->getImgDataLen());
                ss[lenlen] = '\0';
                pv["data"] = std::string(ss);
                delete ss;
                pv["data_len"] = p->getImgDataLen();
                pv["width"] = p->getWidth();
                pv["height"] = p->getHeight();
                pv["x"] = p->getX();
                pv["y"] = p->getY();
                pv["filename"] = p->getFileName();
                v_bkgs.append(pv);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Floor){
                MapFloor *p = static_cast<MapFloor *>(spirit);
                Json::Value pv;
                pv["id"] = p->getId();
                pv["name"] = p->getName();
                pv["bkg"] = p->getBkg();

                Json::Value ppv;
                auto points = p->getPoints();
                int kk = 0;
                for(auto p:points){
                    ppv[kk++] = p;
                }
                pv["points"] = ppv;

                Json::Value ppv2;
                auto paths = p->getPaths();
                int kk2 = 0;
                for(auto p:paths){
                    ppv2[kk2++] = p;
                }
                pv["paths"] = ppv2;
                v_floors.append(pv);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Block){
                MapBlock *p = static_cast<MapBlock *>(spirit);
                Json::Value pv;
                pv["id"] = p->getId();
                pv["name"] = p->getName();

                Json::Value ppv;
                auto ps = p->getSpirits();
                int kk = 0;
                for(auto p:ps){
                    ppv[kk++] = p;
                }
                pv["spirits"] = ppv;
                v_blocks.append(pv);
            }
            else if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Group){
                MapGroup *p = static_cast<MapGroup *>(spirit);
                Json::Value pv;
                pv["id"] = p->getId();
                pv["name"] = p->getName();
                Json::Value ppv;
                auto ps = p->getSpirits();
                int kk = 0;
                for(auto p:ps){
                    ppv[kk++] = p;
                }
                pv["spirits"] = ppv;
                Json::Value ppv2;
                auto pps = p->getAgvs();
                kk = 0;
                for(auto p:pps){
                    ppv2[kk++] = p;
                }
                pv["agvs"] = ppv2;
                v_groups.append(pv);
            }
        }
        response["points"] = v_points;
        response["paths"] = v_paths;
        response["floors"] = v_floors;
        response["bkgs"] = v_bkgs;
        response["blocks"] = v_blocks;
        response["groups"] = v_groups;
        response["maxId"] = g_onemap.getMaxId();
    }
    conn->send(response);
}

void MapManager::interTrafficControlStation(qyhnetwork::TcpSessionPtr conn, const Json::Value &request)
{
    Json::Value response;
    response["type"] = MSG_TYPE_RESPONSE;
    response["todo"] = request["todo"];
    response["queuenumber"] = request["queuenumber"];
    response["result"] = RETURN_MSG_RESULT_SUCCESS;

    if (mapModifying) {
        response["result"] = RETURN_MSG_RESULT_FAIL;
        response["error_code"] = RETURN_MSG_ERROR_CODE_CTREATING;
    }
    else {
        int id = request["id"].asInt();
        MapSpirit *spirit = g_onemap.getSpiritById(id);
        if(spirit == nullptr){
            response["result"] = RETURN_MSG_RESULT_FAIL;
            response["error_code"] = RETURN_MSG_ERROR_CODE_UNFINDED;
        }else{
            if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Point){
                try{
                    CppSQLite3Buffer bufSQL;
                    MapPoint *station = static_cast<MapPoint *>(spirit);
                    bufSQL.format("update agv_station set locked = 1 where id = %d;",station->getId());
                    g_db.execDML(bufSQL);
                    station->setLocked(true);
                }
                catch (CppSQLite3Exception e) {
                    response["result"] = RETURN_MSG_RESULT_FAIL;
                    response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
                    std::stringstream ss;
                    ss << "code:" << e.errorCode() << " msg:" << e.errorMessage();
                    response["error_info"] = ss.str();
                    combined_logger->error("sqlerr code:{0} msg:{1}",e.errorCode(), e.errorMessage());
                }
                catch (std::exception e) {
                    response["result"] = RETURN_MSG_RESULT_FAIL;
                    response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
                    std::stringstream ss;
                    ss << "info:" << e.what();
                    response["error_info"] = ss.str();
                    combined_logger->error("sqlerr code:{0}",e.what());
                }
            }else {
                response["result"] = RETURN_MSG_RESULT_FAIL;
                response["error_code"] = RETURN_MSG_ERROR_CODE_UNFINDED;
                response["error_info"] = "not path or point";
            }
        }
    }
    conn->send(response);
}

void MapManager::interTrafficReleaseLine(qyhnetwork::TcpSessionPtr conn, const Json::Value &request)
{
    Json::Value response;
    response["type"] = MSG_TYPE_RESPONSE;
    response["todo"] = request["todo"];
    response["queuenumber"] = request["queuenumber"];
    response["result"] = RETURN_MSG_RESULT_SUCCESS;

    if (mapModifying) {
        response["result"] = RETURN_MSG_RESULT_FAIL;
        response["error_code"] = RETURN_MSG_ERROR_CODE_CTREATING;
    }
    else {
        int id = request["id"].asInt();
        MapSpirit *spirit = g_onemap.getSpiritById(id);
        if(spirit == nullptr){
            response["result"] = RETURN_MSG_RESULT_FAIL;
            response["error_code"] = RETURN_MSG_ERROR_CODE_UNFINDED;
        }else{
            if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Path){
                try{
                    CppSQLite3Buffer bufSQL;
                    MapPath *path = static_cast<MapPath *>(spirit);
                    bufSQL.format("update agv_line set locked = 0 where id = %d;",path->getId());
                    g_db.execDML(bufSQL);
                    path->setLocked(false);
                }
                catch (CppSQLite3Exception e) {
                    response["result"] = RETURN_MSG_RESULT_FAIL;
                    response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
                    std::stringstream ss;
                    ss << "code:" << e.errorCode() << " msg:" << e.errorMessage();
                    response["error_info"] = ss.str();
                    combined_logger->error("sqlerr code:{0} msg:{1}",e.errorCode(), e.errorMessage());
                }
                catch (std::exception e) {
                    response["result"] = RETURN_MSG_RESULT_FAIL;
                    response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
                    std::stringstream ss;
                    ss << "info:" << e.what();
                    response["error_info"] = ss.str();
                    combined_logger->error("sqlerr code:{0}",e.what());
                }

            }else{
                response["result"] = RETURN_MSG_RESULT_FAIL;
                response["error_code"] = RETURN_MSG_ERROR_CODE_UNFINDED;
                response["error_info"] = "not path or point";
            }
        }
    }
    conn->send(response);
}


void MapManager::interTrafficReleaseStation(qyhnetwork::TcpSessionPtr conn, const Json::Value &request)
{
    Json::Value response;
    response["type"] = MSG_TYPE_RESPONSE;
    response["todo"] = request["todo"];
    response["queuenumber"] = request["queuenumber"];
    response["result"] = RETURN_MSG_RESULT_SUCCESS;

    if (mapModifying) {
        response["result"] = RETURN_MSG_RESULT_FAIL;
        response["error_code"] = RETURN_MSG_ERROR_CODE_CTREATING;
    }
    else {
        int id = request["id"].asInt();
        MapSpirit *spirit = g_onemap.getSpiritById(id);
        if(spirit == nullptr){
            response["result"] = RETURN_MSG_RESULT_FAIL;
            response["error_code"] = RETURN_MSG_ERROR_CODE_UNFINDED;
        }else{
            if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Point){
                try{
                    CppSQLite3Buffer bufSQL;
                    MapPoint *station = static_cast<MapPoint *>(spirit);
                    bufSQL.format("update agv_station set locked = 0 where id = %d;",station->getId());
                    g_db.execDML(bufSQL);
                    station->setLocked(false);
                }
                catch (CppSQLite3Exception e) {
                    response["result"] = RETURN_MSG_RESULT_FAIL;
                    response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
                    std::stringstream ss;
                    ss << "code:" << e.errorCode() << " msg:" << e.errorMessage();
                    response["error_info"] = ss.str();
                    combined_logger->error("sqlerr code:{0} msg:{1}",e.errorCode(), e.errorMessage());
                }
                catch (std::exception e) {
                    response["result"] = RETURN_MSG_RESULT_FAIL;
                    response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
                    std::stringstream ss;
                    ss << "info:" << e.what();
                    response["error_info"] = ss.str();
                    combined_logger->error("sqlerr code:{0}",e.what());
                }
            }else{
                response["result"] = RETURN_MSG_RESULT_FAIL;
                response["error_code"] = RETURN_MSG_ERROR_CODE_UNFINDED;
                response["error_info"] = "not path or point";
            }
        }
    }
    conn->send(response);
}

void MapManager::interTrafficControlLine(qyhnetwork::TcpSessionPtr conn, const Json::Value &request)
{
    Json::Value response;
    response["type"] = MSG_TYPE_RESPONSE;
    response["todo"] = request["todo"];
    response["queuenumber"] = request["queuenumber"];
    response["result"] = RETURN_MSG_RESULT_SUCCESS;

    if (mapModifying) {
        response["result"] = RETURN_MSG_RESULT_FAIL;
        response["error_code"] = RETURN_MSG_ERROR_CODE_CTREATING;
    }
    else {
        int id = request["id"].asInt();
        MapSpirit *spirit = g_onemap.getSpiritById(id);
        if(spirit == nullptr){
            response["result"] = RETURN_MSG_RESULT_FAIL;
            response["error_code"] = RETURN_MSG_ERROR_CODE_UNFINDED;
        }else{
            if(spirit->getSpiritType() == MapSpirit::Map_Sprite_Type_Path){
                try{
                    CppSQLite3Buffer bufSQL;
                    MapPath *path = static_cast<MapPath *>(spirit);
                    bufSQL.format("update agv_line set locked = 1 where id = %d;",path->getId());
                    g_db.execDML(bufSQL);
                    path->setLocked(true);
                }
                catch (CppSQLite3Exception e) {
                    response["result"] = RETURN_MSG_RESULT_FAIL;
                    response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
                    std::stringstream ss;
                    ss << "code:" << e.errorCode() << " msg:" << e.errorMessage();
                    response["error_info"] = ss.str();
                    combined_logger->error("sqlerr code:{0} msg:{1}",e.errorCode(), e.errorMessage());
                }
                catch (std::exception e) {
                    response["result"] = RETURN_MSG_RESULT_FAIL;
                    response["error_code"] = RETURN_MSG_ERROR_CODE_QUERY_SQL_FAIL;
                    std::stringstream ss;
                    ss << "info:" << e.what();
                    response["error_info"] = ss.str();
                    combined_logger->error("sqlerr code:{0}",e.what());
                }

            }else{
                response["result"] = RETURN_MSG_RESULT_FAIL;
                response["error_code"] = RETURN_MSG_ERROR_CODE_UNFINDED;
                response["error_info"] = "not path or point";
            }
        }
    }
    conn->send(response);
}