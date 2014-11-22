/*
 * DownlaodQueue.h
 *
 *  Created on: 2014-07-17
 *      Author: Behrooz Shafiee Sarjaz
 */

#ifndef DOWNLOADQUEUE_H_
#define DOWNLOADQUEUE_H_
#include "SyncQueue.h"
#include "Backend.h"

namespace FUSESwift {

class DownloadQueue: public SyncQueue{
  //Process Events
  void processEvent(const SyncEvent* _event);
  static void syncLoopWrapper();
  void syncLoop();
  void processDownloadContent(const SyncEvent* _event);
  void processDownloadMetadata(const SyncEvent* _event);
  //Private constructor
  DownloadQueue();
public:
  static DownloadQueue& getInstance();
  virtual ~DownloadQueue();
  //Start Downlaod Thread
  void startSynchronization();
  //Stop Downlaod Thread
  void stopSynchronization();
  //Add Download assignment from zoo
  void addZooTask(vector<string>assignments);
};

} /* namespace FUSESwift */
#endif /* DOWNLOADQUEUE_H_ */