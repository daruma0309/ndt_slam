#include "ndt_slam/PointCloudMap.h"
using namespace std;

pcl::PointCloud<pcl::PointXYZ>::Ptr Submap::filterPoints() {
  pcl::PointCloud<pcl::PointXYZ>::Ptr filtered_cloud(new pcl::PointCloud<pcl::PointXYZ>);

  pcl::ApproximateVoxelGrid<pcl::PointXYZ> approximate_voxel_filter;
  approximate_voxel_filter.setLeafSize(LeafSize, LeafSize, LeafSize);
  approximate_voxel_filter.setInputCloud(p_cloud);
  approximate_voxel_filter.filter(*filtered_cloud);

  return filtered_cloud;
}

void Submap::makeMap() {
  p_cloud->clear();

  if (removeMoving == true) {
    if (cntS == 0) *p_cloud += *(scans[0]);
    int scan_num = static_cast<int>(scans.size());
    for (int i = 0; i < scan_num-2; i++) {
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_1and3(new pcl::PointCloud<pcl::PointXYZ>);
      *cloud_1and3 += *(scans[i]);
      *cloud_1and3 += *(scans[i+2]);
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_diff(new pcl::PointCloud<pcl::PointXYZ>);
      cloud_diff = pcf.difference_extraction(cloud_1and3, scans[i+1]);
      *p_cloud += *(pcf.remove_neighborPoint(scans[i+1], cloud_diff));
    }
    if (newest == true) {
      *p_cloud += *(scans[scans.size()-1]);
    }
  } else {
    if (cntS == 0) {  // 最初の部分地図なら
      for (size_t i = 0; i < scans.size(); i++) *p_cloud += *(scans[i]);
    } else {
      for (size_t i = 2; i < scans.size(); i++) *p_cloud += *(scans[i]);
    }
  }
}

///////////////////////

// ロボット位置の追加
void PointCloudMap::addPose(const Pose2D &p) {
  // 累積走行距離(atd)の計算
  if (poses.size() > 0) {
    Pose2D pp = poses.back(); // 一つ前(posesの末尾)のロボット位置
    atd += sqrt((p.tx - pp.tx)*(p.tx - pp.tx) + (p.ty - pp.ty)*(p.ty - pp.ty));
  }
  else {
    atd = 0.0;
  }

  poses.emplace_back(p);
}

// スキャン点群の追加
void PointCloudMap::addPoints(const std::vector<LPoint2D> &lps) {
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_ptr(new pcl::PointCloud<pcl::PointXYZ>);
  cloud_ptr->width = lps.size();
  cloud_ptr->height = 1;
  cloud_ptr->is_dense = false;
  cloud_ptr->points.resize(cloud_ptr->width * cloud_ptr->height);
  for (size_t i = 0; i < cloud_ptr->points.size(); i++) {
    cloud_ptr->points[i].x = lps[i].x;
    cloud_ptr->points[i].y = lps[i].y;
    cloud_ptr->points[i].z = 0;
  }

  Submap &curSubmap = submaps.back();              // 現在の部分地図

  if (atd - curSubmap.atdS >= sepThre ) {          // 累積走行距離が閾値を超えたら新しい部分地図に変える
    size_t size = poses.size();                    // posesにはすでに最新値を追加済みなので-2
    curSubmap.cntE = size-2;                       // 部分地図の最後のスキャン番号
    curSubmap.p_cloud = curSubmap.filterPoints();  // フィルター
    curSubmap.newest = false;

    Submap submap(atd, size-1);                      // 新しい部分地図
    size_t scan_num = curSubmap.scans.size();
    if (scan_num >= 2) {                              // 動的物体除去のために, 前の部分地図の最新2データを新しい部分地図に追加
      submap.addPoints(curSubmap.scans[scan_num-2]);
      submap.addPoints(curSubmap.scans[scan_num-1]);
    }
    submap.addPoints(cloud_ptr);                   // スキャン点群の登録
    submap.makeMap();                              // 地図再構築 + 動的物体除去
    submaps.emplace_back(submap);                  // 部分地図を追加
  } else {                                         // 超えていなければ
    curSubmap.addPoints(cloud_ptr);                // 現在の部分地図に点群を追加
    curSubmap.makeMap();                           // 地図再構築 + 動的物体除去
  }
}

///////////

// 全体地図生成 局所地図もここで生成
void PointCloudMap::makeGlobalMap(){
  globalMap_cloud->clear();                               // 初期化
  maps.clear();

  // 現在以外のすでに確定した部分地図を全体地図に入れる
  for (size_t i=0; i<submaps.size()-1; i++) {
    Submap &submap = submaps[i];                   // 部分地図
    *globalMap_cloud += *(submap.p_cloud);
    
    maps.emplace_back(submap.p_cloud);  // 部分地図ごとに保存
  }

  // 現在の部分地図を全体地図に入れる
  Submap &curSubmap = submaps.back();              // 現在の部分地図
  pcl::PointCloud<pcl::PointXYZ>::Ptr filtered_cloud = curSubmap.filterPoints(); // フィルター
  *globalMap_cloud += *filtered_cloud;
  maps.emplace_back(filtered_cloud);  // 部分地図ごとに保存

  ROS_INFO("[PointCloudMap::makeGlobalMap] curSubmap.atdS=%g, atd=%g", curSubmap.atdS, atd);
  ROS_INFO("[PointCloudMap::makeGlobalMap] submaps.size=%lu, lobalMap_cloud->points.size=%lu", submaps.size(), globalMap_cloud->points.size());
}

// 局所地図生成
void PointCloudMap::makeLocalMap() {
  localMap_cloud->clear();                                // 初期化

  // 最新の地図だけではデータ数が少ないので 直前の部分地図も使う
  if (submaps.size() >= 2) {
    Submap &submap = submaps[submaps.size()-2];    // 直前の部分地図だけ使う
    *localMap_cloud += *(submap.p_cloud);
  }

  // 現在の部分地図の代表点を局所地図に入れる
  Submap &curSubmap = submaps.back();              // 現在の部分地図
  pcl::PointCloud<pcl::PointXYZ>::Ptr filtered_cloud = curSubmap.filterPoints(); // フィルター
  *localMap_cloud += *filtered_cloud;

  ROS_INFO("[PointCloudMap::makeLocalMap] localMap_cloud->points.size=%lu", localMap_cloud->points.size());   // 確認用
}

/*
// ポーズ調整後のロボット軌跡newPoseを用いて、地図を再構築する
void PointCloudMap::remakeMaps(const std::vector<Pose2D> &newPoses) {
  // 各部分地図内の点の位置を修正する
  for (size_t i=0; i<submaps.size(); i++) {
    Submap &submap = submaps[i];
    vector<LPoint2D> &mps = submap.lps;                // 部分地図の点群。現在地図以外は代表点になっている
    for (size_t j=0; j<mps.size(); j++) {
      LPoint2D &mp = mps[j];
      size_t idx = mp.sid - startFrame;                          // 点のスキャン番号

      const Pose2D &oldPose = poses[idx];              // mpに対応する古いロボット位置
      const Pose2D &newPose = newPoses[idx];           // mpに対応する新しいロボット位置
      const double (*R1)[2] = oldPose.Rmat;
      const double (*R2)[2] = newPose.Rmat;
      LPoint2D lp1 = oldPose.relativePoint(mp);        // oldPoseでmpをセンサ座標系に変換
      LPoint2D lp2 = newPose.globalPoint(lp1);         // newPoseでポーズ調整後の地図座標系に変換
      mp.x = lp2.x;
      mp.y = lp2.y;
      double nx = R1[0][0]*mp.nx + R1[1][0]*mp.ny;     // 法線ベクトルもoldPoseでセンサ座標系に変換
      double ny = R1[0][1]*mp.nx + R1[1][1]*mp.ny;
      double nx2 = R2[0][0]*nx + R2[0][1]*ny;          // 法線ベクトルもnewPoseでポーズ調整後の地図座標系に変換
      double ny2 = R2[1][0]*nx + R2[1][1]*ny;
      mp.setNormal(nx2, ny2);
    }
  }

  makeGlobalMap();                                     // 部分地図から全体地図と局所地図を生成
  makeLocalMap();

  for (size_t i=0; i<poses.size(); i++) {              // posesをポーズ調整後の値に更新
    poses[i] = newPoses[i];
  }
  lastPose = newPoses.back();
}
*/
