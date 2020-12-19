#pragma once
#pragma comment( lib, "bakkesmod.lib" )
#pragma comment (lib, "ws2_32")
#pragma comment (lib, "Crypt32")
#pragma comment (lib, "Wldap32")
//#pragma comment(lib, "ViGEmClient.lib")
#define NOMINMAX
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include <iostream>
#include <fstream>
#include <sstream> 
#include <iterator>
#include <math.h> 
#include <windows.h>
// DS4 Emulator (ViGEm)
#include <stdlib.h>
#include <cstdio>
#include <ViGEm/Client.h>
#define WIN32_LEAN_AND_MEAN
// Keras, TensorFlow
#include <fdeep/fdeep.hpp>
// Azure File Storage
//#include <was/storage_account.h>
//#include <was/file.h>
// Time tests
#include <chrono>
// Dropbox File Storage
#include <stdio.h>
#include <curl/curl.h>
// Threading
#include <thread>


class nn_data_plugin : public BakkesMod::Plugin::BakkesModPlugin
{
//private:
//	shared_ptr<bool> enable_recording;
public:
	virtual void onLoad();
	virtual void onUnload();
	virtual void nn_loop();
	virtual void stopRecordingAtGoalScore();
	virtual void wait3seconds();
	virtual void startRecordingAfterKickoffTimer();
	virtual void makenewfile();
	virtual void match_ended();
	virtual void OnCarJump(CarWrapper car, void* params, std::string funcName);
	virtual void OnCarDoubleJump(CarWrapper car, void* params, std::string funcName);
	virtual void OnCarDodge(CarWrapper car, void* params, std::string funcName);
	//virtual void OnCarFlip(CarWrapper car, void* params, std::string funcName);
	bool jump_switch = true;
	bool jump_now = false;
	//virtual void upload_dropbox_data();
	bool is_recording = false;
	bool new_file_created = false;
	ofstream data_file;
	int filecount = 0;
	string filepath;
	DS4_REPORT report;
	PVIGEM_CLIENT driver;
	PVIGEM_TARGET ds4;
	VIGEM_ERROR ret;
	static fdeep::model* fmodel;
	std::vector<float> model_means;
	std::vector<float> model_stds;
	//azure::storage::cloud_file_directory fileshare_root_dir;
	string my_steam_identity;
	string my_mmr_saved = "0";
	string opponent_mmr_saved = "0";
	std::chrono::high_resolution_clock::time_point saved_time;
	int last_mode_played = 0;
	int loop_count = 0;
	bool enable_bot = true;
	bool enable_bot_control = true;
	bool enable_data_recording = false;
	bool normalize_inputs = false;
};
