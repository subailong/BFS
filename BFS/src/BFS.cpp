//============================================================================
// Name        : BFS.cpp
// Author      : Behrooz Shafiee Sarjaz
// Version     :
// Copyright   : 2014 Behrooz
//============================================================================

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// The FUSE API has been changed a number of times.  So, our code
// needs to define the version of the API that we assume.  As of this
// writing, the most current API version is 26
#define FUSE_USE_VERSION 29

#include <signal.h>
#include "params.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <Account.h>
#include <Container.h>
#include <Object.h>
#include <iostream>
#include <istream>
#include <Poco/StreamCopier.h>
#include "FUSESwift.h"
#include "log.h"
#include "filesystem.h"
#include "filenode.h"
#include "SyncQueue.h"
#include "SwiftBackend.h"
#include "BackendManager.h"
#include <string.h>
#include "DownloadQueue.h"
#include "UploadQueue.h"
#include "MemoryController.h"
#include "SettingManager.h"
#include "BFSNetwork.h"
#include "MasterHandler.h"
#include "ZooHandler.h"
#include <thread>

//Initialize logger
#include "LoggerInclude.h"
_INITIALIZE_EASYLOGGINGPP


using namespace Swift;
using namespace FUSESwift;
using namespace std;
using namespace Poco;


void shutdown(void* userdata);

static struct fuse_operations fuse_oper = {
  .getattr = FUSESwift::swift_getattr ,
  .readlink = FUSESwift::swift_readlink ,
  .getdir = NULL ,
  .mknod = FUSESwift::swift_mknod ,
  .mkdir = FUSESwift::swift_mkdir ,
  .unlink = FUSESwift::swift_unlink ,
  .rmdir = FUSESwift::swift_rmdir ,
  .symlink = NULL ,
  .rename = FUSESwift::swift_rename ,
  .link = NULL ,
  .chmod = NULL ,
  .chown = NULL ,
  .truncate = FUSESwift::swift_truncate ,
  .utime = NULL ,
  .open = FUSESwift::swift_open ,
  .read = FUSESwift::swift_read ,
  .write = FUSESwift::swift_write ,
  .statfs = NULL ,
  .flush = FUSESwift::swift_flush ,
  .release = FUSESwift::swift_release ,
  .fsync = NULL ,
  .setxattr = NULL ,
  .getxattr = NULL ,
  .listxattr = NULL ,
  .removexattr = NULL ,
  .opendir = FUSESwift::swift_opendir ,
  .readdir = FUSESwift::swift_readdir ,
  .releasedir = FUSESwift::swift_releasedir ,
  .fsyncdir = NULL ,
  .init = FUSESwift::swift_init ,
  .destroy = shutdown ,
  .access = FUSESwift::swift_access ,
  .create = NULL ,
  .ftruncate = FUSESwift::swift_ftruncate ,
  .fgetattr = NULL ,
  .lock = NULL ,
  .utimens = NULL ,
  .bmap = NULL ,
  .flag_nullpath_ok = 1,
  .flag_nopath = 1,
  .flag_utime_omit_ok = 1,
  .flag_reserved = 29,
  .ioctl = NULL ,
  .poll = NULL ,
  .write_buf = NULL ,
  .read_buf = NULL ,
  .flock = NULL ,
  .fallocate = NULL
};

void shutdown(void* userdata) {
  LOG(INFO) <<"Leaving...";
  //Log Close
  log_close();
  BFSNetwork::stopNetwork();
  ZooHandler::getInstance().stopZooHandler();
  MasterHandler::stopLeadership();
  FileSystem::getInstance().destroy();
  exit(0);
}

void bfs_usage(){
	fprintf(stderr, "usage:  BFS [FUSE and mount options] mountPoint\n");
  shutdown(nullptr);
}

void sigproc(int sig) {
	shutdown(nullptr);
}

// function to call if operator new can't allocate enough memory or error arises
void systemErrorHandler() {
  LOG(ERROR) <<"System Termination Occurred";
  shutdown(nullptr);
}

// function to call if operator new can't allocate enough memory or error arises
void outOfMemHandler() {
  LOG(ERROR) <<"Unable to satisfy request for memory";
  shutdown(nullptr);
}

atomic<int> global(0);
void readRemote() {
  //size_t len = 1024l*1024l*1024l;
  size_t len = 102400;
  unsigned char mac[6] = {0x90,0xe2,0xba,0x35,0x22,0xd8};
  char * buffer = new char[len];
  //long res = BFSNetwork::readRemoteFile((void*)buffer,len,(size_t)0,string("/RNA"),mac);
  //long res = BFSNetwork::writeRemoteFile(buffer,len,123,"/RNA",mac);
  //struct stat attBuff;
  //long res = BFSNetwork::readRemoteFileAttrib(&attBuff,string("/RNA"),mac);
  //long res = BFSNetwork::deleteRemoteFile(string("/2G"),mac);
  //long res = BFSNetwork::truncateRemoteFile(string("/trunc"),len,mac);
  long res = BFSNetwork::createRemoteFile(string("/trunc2"),mac);


  LOG(INFO) <<"READ DONE:"<<res<<" "<<++global;
  delete []buffer;
  //buffer = nullptr;
}

void testRemoteRead() {
  sleep(1);
  for(int i=0;i<1;i++) {
    //usleep(100);
    //readRemote();
    new thread(readRemote);
  }
  //readRemote();
}

void initLogger(int argc, char *argv[]) {
  _START_EASYLOGGINGPP(argc, argv);
  // Load configuration from file
  el::Configurations conf("log_config");
  // Reconfigure single logger
  el::Loggers::reconfigureLogger("default", conf);
  // Actually reconfigure all loggers instead
  el::Loggers::reconfigureAllLoggers(conf);

  el::Loggers::addFlag(el::LoggingFlag::LogDetailedCrashReason);
}

int main(int argc, char *argv[]) {
  initLogger(argc,argv);
	//Load configs first
	SettingManager::load("config");

	//Set signal handlers
	signal(SIGINT, sigproc);
	signal(SIGTERM, sigproc);
	signal(SIGINT, sigproc);
  //set the new_handler
  //std::set_new_handler(outOfMemHandler);
  //std::set_terminate(systemErrorHandler);

  AuthenticationInfo info;
  info.username = SettingManager::get(CONFIG_KEY_SWIFT_USERNAME);
  info.password = SettingManager::get(CONFIG_KEY_SWIFT_PASSWORD);
  info.authUrl = SettingManager::get(CONFIG_KEY_SWIFT_URL);
  info.tenantName = SettingManager::get(CONFIG_KEY_SWIFT_TENANT);
  info.method = AuthenticationMethod::KEYSTONE;

  //make ready log file
  log_open();

  // BFS doesn't do any access checking on its own (the comment
  // blocks in fuse.h mention some of the functions that need
  // accesses checked -- but note there are other functions, like
  // chown(), that also need checking!).  Since running bbfs as root
  // will therefore open Metrodome-sized holes in the system
  // security, we'll check if root is trying to mount the filesystem
  // and refuse if it is.  The somewhat smaller hole of an ordinary
  // user doing it with the allow_other flag is still there because
  // I don't want to parse the options string.
  if ((getuid() == 0) || (geteuid() == 0)) {
    LOG(ERROR) << "Running BBFS as root opens unnacceptable security holes";
    //return 1;
  }

  // Sanity check
  if (argc < 1)
    bfs_usage();


  SwiftBackend swiftBackend;
  swiftBackend.initialize(&info);
  BackendManager::registerBackend(&swiftBackend);

  //Get Physical Memory amount
  LOG(INFO) <<"Total Physical Memory:" << MemoryContorller::getInstance().getTotalSystemMemory()/1024/1024 << " MB";
  LOG(INFO) <<"BFS Available Memory:" << MemoryContorller::getInstance().getAvailableMemory()/1024/1024 << " MB";
  LOG(ERROR) <<"Memory Utilization:" << MemoryContorller::getInstance().getMemoryUtilization()*100<< "%";

  //Start BFS Network(before zoo, zoo uses mac info from this package)
	if(!BFSNetwork::startNetwork()) {
	  LOG(ERROR) <<"Cannot initialize ZeroNetworking!";
		shutdown(nullptr);
	}

	//testRemoteRead();

  // turn over control to fuse
	LOG(INFO) <<"calling fuse_main";
  //Start fuse_main
  int fuse_stat = 0;
  fuse_stat = fuse_main(argc, argv, &fuse_oper, nullptr);
  LOG(ERROR) <<"fuse returned: "<<fuse_stat;
  //while(1) {sleep(1);}

  shutdown(nullptr);
  return fuse_stat;
}