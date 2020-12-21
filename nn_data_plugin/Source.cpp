#include "nn_data.h"
#include "bakkesmod/wrappers/GameObject/BallWrapper.h"
#include "bakkesmod/wrappers/GameObject/PriWrapper.h"
#include "bakkesmod/wrappers/GameObject/FXActorWrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"
#include "bakkesmod/wrappers/GameEvent/GameEventWrapper.h"
#include "bakkesmod/wrappers/GameObject/CarWrapper.h"
#include "bakkesmod/wrappers/ArrayWrapper.h"
#include "bakkesmod/wrappers/PlayerControllerWrapper.h"

using namespace std;
using namespace std::chrono;



BAKKESMOD_PLUGIN(nn_data_plugin, "Neural Net Data Recorder", "0.1", 0)

//UNCOMMENT THIS IF USING FDEEP
//fdeep::model* nn_data_plugin::fmodel = NULL;

string space2newline(string text)
{
	for (string::iterator it = text.begin(); it != text.end(); ++it)
	{
		if (*it == ' ')
		{
			*it = '\n';
		}
	}
	return text;
}


string ExePath() 
{
	char buffer[MAX_PATH];
	GetModuleFileName(NULL, buffer, MAX_PATH);
	string::size_type pos = string(buffer).find_last_of("\\/");
	return string(buffer).substr(0, pos);
}

void createDataDirectory(string filepath) 
{
	if (CreateDirectory(filepath.c_str(), NULL))
	{
		// Directory created
	}
	else if (ERROR_ALREADY_EXISTS == GetLastError())
	{
		// Directory already exists
	}
	else
	{
		// Failed for some other reason
	}
}



// --DS4 COMMENT SECTION BELOW--

VOID my_ds4_callback(
	PVIGEM_CLIENT Client,
	PVIGEM_TARGET Target,
	UCHAR LargeMotor,
	UCHAR SmallMotor,
	DS4_LIGHTBAR_COLOR LightbarColor)
{
	printf("DS4 Response - Serial: %d, LM: %d, SM: %d, R: %d, G: %d, B: %d\n",
		vigem_target_get_index(Target),
		LargeMotor,
		SmallMotor,
		LightbarColor.Red,
		LightbarColor.Green,
		LightbarColor.Blue);
}

// VOID my_target_add_callback(
// 	PVIGEM_CLIENT Client,
// 	PVIGEM_TARGET Target,
// 	VIGEM_ERROR Result)
// {
// 	printf("Target with type %d and serial %d is live with result %d\n",
// 		vigem_target_get_type(Target),
// 		vigem_target_get_index(Target), Result);

// 	if (!VIGEM_SUCCESS(Result))
// 	{
// 		printf("Device plugin didn't work!\n");
// 		return;
// 	}

// 	//
// 	// Register PVIGEM_DS4_NOTIFICATION on success
// 	// 
// 	auto ret = vigem_target_ds4_register_notification(Client, Target, reinterpret_cast<PFN_VIGEM_DS4_NOTIFICATION>(my_ds4_callback));
// 	if (!VIGEM_SUCCESS(ret))
// 	{
// 		printf("Couldn't add DS4 notification callback\n");
// 	}

// 	DS4_REPORT report;
// 	DS4_REPORT_INIT(&report);
// }

void nn_data_plugin::onLoad()
{	
	// Opponent jump detection
	gameWrapper->HookEventWithCaller<CarWrapper>("Function CarComponent_Jump_TA.Active.BeginState", std::bind(&nn_data_plugin::OnCarJump, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	gameWrapper->HookEventWithCaller<CarWrapper>("Function CarComponent_DoubleJump_TA.Active.BeginState", std::bind(&nn_data_plugin::OnCarDoubleJump, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	gameWrapper->HookEventWithCaller<CarWrapper>("Function CarComponent_Dodge_TA.Active.BeginState", std::bind(&nn_data_plugin::OnCarDodge, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	//gameWrapper->HookEventWithCaller<CarWrapper>("	Function TAGame.CarComponent_FlipCar_TA.InitFlip", std::bind(&nn_data_plugin::OnCarFlip, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	//Filter times when game is not in play (replays + kickoff)
	gameWrapper->HookEvent("Function TAGame.Team_TA.EventScoreUpdated", bind(&nn_data_plugin::stopRecordingAtGoalScore, this));
	gameWrapper->HookEvent("Function GameEvent_TA.Countdown.BeginState", bind(&nn_data_plugin::wait3seconds, this));

	if (this->enable_data_recording)
	{
		//Create new data file when match starts
		gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.InitField", bind(&nn_data_plugin::makenewfile, this));
		//Upload data on match end
		//gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.EventMatchEnded", bind(&nn_data_plugin::match_ended, this));
		gameWrapper->HookEvent("Function TAGame.GameEvent_TA.Destroyed", bind(&nn_data_plugin::match_ended, this));
	}
	gameWrapper->HookEvent("Function TAGame.Car_TA.SetVehicleInput", bind(&nn_data_plugin::nn_loop, this));
	// Init steamID
	this->my_steam_identity = "0";

	//Set data save filepath
	this->filepath = ExePath() + "\\bakkesmod\\plugins\\nn_data\\";
	createDataDirectory(this->filepath);
}



void nn_data_plugin::onUnload()
{

}

void nn_data_plugin::OnCarJump(CarWrapper car, void* params, std::string funcName)
{
	cvarManager->log("1 JUMP");
	if (this->jump_switch) {
		this->jump_now = 1;
	}
	this->jump_switch = !this->jump_switch;
}

void nn_data_plugin::OnCarDoubleJump(CarWrapper car, void* params, std::string funcName)
{
	cvarManager->log("2 JUMP");
	if (this->jump_switch) {
		this->jump_now = 1;
	}
	this->jump_switch = !this->jump_switch;
}

void nn_data_plugin::OnCarDodge(CarWrapper car, void* params, std::string funcName)
{
	cvarManager->log("DODGE");
	if (this->jump_switch) {
		this->jump_now = 1;
	}
	this->jump_switch = !this->jump_switch;
}

//void nn_data_plugin::OnCarFlip(CarWrapper car, void* params, std::string funcName)
//{
//	cvarManager->log("FLIP");
//
//}

void nn_data_plugin::match_ended()
{
	auto upload_game_file = [](string filenum, string upload_filepath, string web_filename) {
		
		std::ifstream t(upload_filepath);
		std::string data_buf((std::istreambuf_iterator<char>(t)),
			std::istreambuf_iterator<char>());

		CURL* curl;
		CURLcode res;
		curl_global_init(CURL_GLOBAL_ALL);
		curl = curl_easy_init();
		if (curl) 
		{
			string dropbox_file_name = "Dropbox-API-Arg: {\"path\":\"/" + web_filename + "\",\"autorename\": true}";

			struct curl_slist* headers = NULL; /* init to NULL is important */
			headers = curl_slist_append(headers, "Authorization: Bearer j87IvoIzsuYAAAAAAAAIl0BCIOa9y1PUECIQj8aB-tpDGkvEpMPlvIJD2nvYW0iV");
			headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
			//headers = curl_slist_append(headers, "Dropbox-API-Arg: {\"path\":\"/test_c++_upload_test2.txt\",\"autorename\": true}");
			headers = curl_slist_append(headers, dropbox_file_name.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

			curl_easy_setopt(curl, CURLOPT_URL, "https://content.dropboxapi.com/2/files/upload");
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data_buf);

			/* Perform the request, res will get the return code */
			res = curl_easy_perform(curl);
			/* Check for errors */
			if (res != CURLE_OK)
			{
				//cvarManager->log("Curl failed");
				//fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			}

			/* always cleanup */
			curl_easy_cleanup(curl);
		}
		curl_global_cleanup();
	};

	if (this->last_mode_played == 1 or this->last_mode_played == 10)
	{
		cvarManager->log("UPLOADING DATA");

		// Get txt file name
		string searchy = this->filepath + "*";
		int n = searchy.length();
		char* DirSpec2 = new char[n + 1];
		strcpy_s(DirSpec2, n + 1, searchy.c_str());

		WIN32_FIND_DATA data;
		HANDLE hFind;
		vector<string> v;
		if ((hFind = FindFirstFile(DirSpec2, &data)) != INVALID_HANDLE_VALUE) {
			do {
				v.push_back(data.cFileName);
			} while (FindNextFile(hFind, &data) != 0);
			FindClose(hFind);

		}
		delete [] DirSpec2; // Prevent memory leak
		string filenum = to_string(v.size() - 2);

		string upload_filepath = ExePath() + "\\bakkesmod\\plugins\\nn_data\\" + filenum + ".txt";
		string web_filename = this->my_steam_identity + "_" + filenum + ".txt";

		cvarManager->log(upload_filepath);


		thread upload_data_thread = thread(upload_game_file, filenum, upload_filepath, web_filename);
		upload_data_thread.detach();
	}

	// Reset last mode played and mmrs
	this->last_mode_played = 0;
	this->my_mmr_saved = "0";
	this->opponent_mmr_saved = "0";
}

void nn_data_plugin::stopRecordingAtGoalScore()
{
	this->is_recording = false;
	if (this->enable_data_recording)
	{
		cvarManager->log("Pause Recording");
	}
}

void nn_data_plugin::wait3seconds()
{
	gameWrapper->SetTimeout(bind(&nn_data_plugin::startRecordingAfterKickoffTimer, this), 4.0f);
}

void nn_data_plugin::startRecordingAfterKickoffTimer()
{
	this->is_recording = true;
	if (this->enable_data_recording)
	{
		cvarManager->log("Kickoff Timer Ended");
	}
}

void nn_data_plugin::makenewfile()
{
	// Close previous file
	if (this->data_file.is_open())
	{
		this->data_file.close();
	}

	//Create new text file for each game 
	string searchy = this->filepath + "*";
	int n = searchy.length();
	char* DirSpec = new char[n + 1];
	strcpy_s(DirSpec, n+1, searchy.c_str());

	WIN32_FIND_DATA data;
	HANDLE hFind;
	vector<string> v;
	if ((hFind = FindFirstFile(DirSpec, &data)) != INVALID_HANDLE_VALUE) {
		do {
			v.push_back(data.cFileName);
		} while (FindNextFile(hFind, &data) != 0);
		FindClose(hFind);
	}
	delete [] DirSpec; // Prevent memory leak
	this->filecount = v.size() - 1;
	this->new_file_created = true;
	//cvarManager->log("Data File: " + to_string(this->filecount) + ".txt");

}


void nn_data_plugin::nn_loop()
{
	

	if (gameWrapper->IsInOnlineGame())
	{
		this->loop_count = this->loop_count + 1;
		// duration is btwn 21000 and 28000 microseconds when checking > 2
		if (this->loop_count >= 1)
		{
			this->loop_count = 0;

			ServerWrapper server = gameWrapper->GetOnlineGame();
			int game_countdown = server.GetSecondsRemaining();
			MMRWrapper mmrw = gameWrapper->GetMMRWrapper();
			int playlistid = mmrw.GetCurrentPlaylist();
			this->last_mode_played = playlistid;


			
			/*Duel: 1
			Doubles : 2
			Standard : 3
			Chaos : 4
			Ranked Duel : 10
			Ranked Doubles : 11
			Ranked Solo Standard : 12
			Ranked Standard : 13
			Mutator Mashup : 14
			Snow Day : 15
			Rocket Labs : 16
			Hoops : 17
			Rumble : 18
			Dropshot : 23*/
			//cvarManager->log("countdown " + to_string(game_countdown) + " Is Null " + to_string(gameWrapper->GetLocalCar().IsNull()));

			//cvarManager->log("OT " + to_string(server.GetbOverTime()));

			if (this->is_recording == true and (game_countdown > 0 or server.GetbOverTime()) ) //mirror in other modes
			{
				//Initialize player vars
				string my_steamid = "NaN";
				string my_mmr = "NaN";
				string my_score = "NaN";
				string my_x = "NaN";
				string my_y = "NaN";
				string my_z = "NaN";
				string my_rotx = "NaN";
				string my_roty = "NaN";
				string my_rotz = "NaN";
				string my_vx = "NaN";
				string my_vy = "NaN";
				string my_vz = "NaN";
				string my_avx = "NaN";
				string my_avy = "NaN";
				string my_avz = "NaN";
				string my_supersonic = "NaN";
				string my_throttle = "NaN";
				string my_steer = "NaN";
				string my_pitch = "NaN";
				string my_yaw = "NaN";
				string my_roll = "NaN";
				string my_jump = "NaN";
				string my_activateboost = "NaN";
				string my_handbrake = "NaN";
				string my_jumped = "NaN";
				string my_team = "NaN";
				string my_boostamount = "NaN";

				string opponent0_steamid = "NaN";
				string opponent0_mmr = "NaN";
				string opponent0_score = "NaN";
				string opponent0_x = "NaN";
				string opponent0_y = "NaN";
				string opponent0_z = "NaN";
				string opponent0_rotx = "NaN";
				string opponent0_roty = "NaN";
				string opponent0_rotz = "NaN";
				string opponent0_vx = "NaN";
				string opponent0_vy = "NaN";
				string opponent0_vz = "NaN";
				string opponent0_avx = "NaN";
				string opponent0_avy = "NaN";
				string opponent0_avz = "NaN";
				string opponent0_supersonic = "NaN";
				string opponent0_throttle = "NaN";
				string opponent0_steer = "NaN";
				string opponent0_pitch = "NaN";
				string opponent0_yaw = "NaN";
				string opponent0_roll = "NaN";
				string opponent0_jump = "NaN";
				string opponent0_activateboost = "NaN";
				string opponent0_handbrake = "NaN";
				string opponent0_jumped = "NaN";
				string opponent0_boostamount = "NaN";

				string ball_x = "NaN"; //toward right if on red team
				string ball_y = "NaN"; //toward red
				string ball_z = "NaN";
				string ball_vx = "NaN";
				string ball_vy = "NaN";
				string ball_vz = "NaN";
				string ball_avx = "NaN";
				string ball_avy = "NaN";
				string ball_avz = "NaN";

				string my_ball_touches = "NaN";
				string opponent0_ball_touches = "NaN";

				string game_countdown_str = "NaN";

				int localPlayerTeam = -5;
				//int localPlayerID;
				string localPlayerName = "bob";
				CarWrapper localPlayer = gameWrapper->GetLocalCar();
				if (!localPlayer.IsNull()) {
					//0=blue team,  1=red team
					localPlayerTeam = localPlayer.GetPRI().GetTeamNum();
					//localPlayerID = localPlayer.GetPRI().GetUniqueId().ID;
					localPlayerName = localPlayer.GetPRI().GetPlayerName().ToString();
					my_team = to_string(localPlayerTeam);
				}

				// flip pos/vel depending on team
				float team_gain = 1.0;
				// if I'm on blue team
				if (localPlayerTeam == 0)
				{
					team_gain = -1.0;
				}

				ArrayWrapper<CarWrapper> cars = server.GetCars();
				for (int i = 0; i < cars.Count(); i++)
				{
					CarWrapper car = cars.Get(i);
					PriWrapper playerInfo = car.GetPRI();

					SteamID steamid = playerInfo.GetUniqueId();

					int ball_touches = playerInfo.GetBallTouches();

					game_countdown_str = to_string(game_countdown);

					bool mmr_already_defined = 0;
					if (this->my_mmr_saved.compare("0") == 0 or this->opponent_mmr_saved.compare("0") == 0)
					{
						// Mine or the opponent's MMR has not been defined
					}
					else
					{
						mmr_already_defined = 1;
					}
					
					float mmr = 0;
					if (mmr_already_defined == 0)
					{
						mmr = mmrw.GetPlayerMMR(steamid, playlistid);
					}
					//float mmr = 1.1;
					int score = playerInfo.GetScore();
					float boost = -1.0;
					if (!car.GetBoostComponent().IsNull())
					{
						boost = car.GetBoostComponent().GetCurrentBoostAmount();
					}

					string name = playerInfo.GetPlayerName().ToString();
					int shortcut = playerInfo.GetSpectatorShortcut();//use this to determine which car is which
					ControllerInput playerInput = car.GetInput();
					if (name.compare(localPlayerName) == 0)
					{
						Vector myPos = car.GetLocation();
						Rotator myRot = car.GetRotation();
						Vector myVel = car.GetVelocity();
						Vector myAngVel = car.GetAngularVelocity();

						my_steamid = to_string(steamid.ID);
						this->my_steam_identity = my_steamid;
						if (mmr_already_defined) {
							my_mmr = this->my_mmr_saved;
						}
						else
						{
							my_mmr = to_string(mmr);
							this->my_mmr_saved = my_mmr;
						}
						my_score = to_string(score);

						my_x = to_string(myPos.X * team_gain); //toward right if on red team
						my_y = to_string(myPos.Y * team_gain); //toward red
						my_z = to_string(myPos.Z);
						my_rotx = to_string(myRot.Pitch); // 0 flat on ground, max positive looking up, max neg looking down
						my_roty = to_string(myRot.Roll); // 0 flat on ground, positive roll right, neg roll left, 32770 when turtle
						my_rotz = to_string(myRot.Yaw); // 32270 or -32270 facing right, positive 16000 facing opposite team goal
						my_vx = to_string(myVel.X * team_gain);
						my_vy = to_string(myVel.Y * team_gain);
						my_vz = to_string(myVel.Z);
						my_avx = to_string(myAngVel.X * team_gain); //facing pos X, roll left
						my_avy = to_string(myAngVel.Y * team_gain); //facing enemy roll right
						my_avz = to_string(myAngVel.Z); //turn right

						my_supersonic = to_string(car.GetbSuperSonic());
						my_throttle = to_string(playerInput.Throttle);
						my_steer = to_string(playerInput.Steer);
						my_pitch = to_string(playerInput.Pitch);
						my_yaw = to_string(playerInput.Yaw);
						my_roll = to_string(playerInput.Roll);
						my_jump = to_string(playerInput.Jump);
						my_activateboost = to_string(playerInput.ActivateBoost);
						my_handbrake = to_string(playerInput.Handbrake);
						my_jumped = to_string(playerInput.Jumped);
						my_boostamount = to_string(boost);

						my_ball_touches = to_string(ball_touches);

						/*cvarManager->log("Mine");
						cvarManager->log(to_string(playerInput.Pitch));
						cvarManager->log(to_string(playerInput.Roll));
						cvarManager->log(to_string(playerInput.Jump));
						cvarManager->log(to_string(playerInput.ActivateBoost));
						cvarManager->log(to_string(playerInput.Handbrake));*/
					}
					else
					{
						Vector opponent0Pos = car.GetLocation();
						Rotator opponent0Rot = car.GetRotation();
						Vector opponent0Vel = car.GetVelocity();
						Vector opponent0AngVel = car.GetAngularVelocity();

						opponent0_steamid = to_string(steamid.ID);

						if (mmr_already_defined) {
							opponent0_mmr = this->opponent_mmr_saved;
						}
						else
						{
							opponent0_mmr = to_string(mmr);
							this->opponent_mmr_saved = opponent0_mmr;
						}
						opponent0_score = to_string(score);

						opponent0_x = to_string(opponent0Pos.X * team_gain * -1.0); //toward right if on red team
						opponent0_y = to_string(opponent0Pos.Y * team_gain * -1.0); //toward red
						opponent0_z = to_string(opponent0Pos.Z);
						opponent0_rotx = to_string(opponent0Rot.Pitch);
						opponent0_roty = to_string(opponent0Rot.Roll);
						opponent0_rotz = to_string(opponent0Rot.Yaw);
						opponent0_vx = to_string(opponent0Vel.X * team_gain * -1.0);
						opponent0_vy = to_string(opponent0Vel.Y * team_gain * -1.0);
						opponent0_vz = to_string(opponent0Vel.Z);
						opponent0_avx = to_string(opponent0AngVel.X * team_gain * -1.0);
						opponent0_avy = to_string(opponent0AngVel.Y * team_gain * -1.0);
						opponent0_avz = to_string(opponent0AngVel.Z);

						opponent0_supersonic = to_string(car.GetbSuperSonic());
						opponent0_throttle = to_string(playerInput.Throttle);
						opponent0_steer = to_string(playerInput.Steer);
						opponent0_pitch = to_string(playerInput.Pitch);
						opponent0_yaw = to_string(playerInput.Yaw);
						opponent0_roll = to_string(playerInput.Roll);
						opponent0_jump = to_string(playerInput.Jump);
						opponent0_activateboost = to_string(playerInput.ActivateBoost);
						opponent0_handbrake = to_string(playerInput.Handbrake);
						opponent0_jumped = to_string(playerInput.Jumped);
						opponent0_boostamount = to_string(boost);

						opponent0_ball_touches = to_string(ball_touches);
						
						/*cvarManager->log("Pitch Roll Jump Boost Brake");
						cvarManager->log(to_string(playerInput.Pitch));
						cvarManager->log(to_string(playerInput.Roll));
						cvarManager->log(to_string(playerInput.Jump));
						cvarManager->log(to_string(playerInput.ActivateBoost));
						cvarManager->log(to_string(playerInput.Handbrake));*/
						/*cvarManager->log("Yaw");
						cvarManager->log(to_string(playerInput.Yaw));
						cvarManager->log("Steer");
						cvarManager->log(to_string(playerInput.Steer));*/
					}

				}

				ArrayWrapper<BallWrapper> balls = server.GetGameBalls();
				if (balls.Count() > 0)
				{
					BallWrapper ball = balls.Get(0);
					Vector ballPos = ball.GetLocation();
					Vector ballVel = ball.GetVelocity();
					Vector ballAngVel = ball.GetAngularVelocity();

					ball_x = to_string(ballPos.X * team_gain); //toward right if on red team
					ball_y = to_string(ballPos.Y * team_gain); //toward red
					ball_z = to_string(ballPos.Z);
					ball_vx = to_string(ballVel.X * team_gain);
					ball_vy = to_string(ballVel.Y * team_gain);
					ball_vz = to_string(ballVel.Z);
					ball_avx = to_string(ballAngVel.X * team_gain);
					ball_avy = to_string(ballAngVel.Y * team_gain);
					ball_avz = to_string(ballAngVel.Z);

					if (this->enable_data_recording)
					{
						// Write data to text file
						string data_line =
							my_team + " " +
							my_steamid + " " +
							my_mmr + " " +
							my_score + " " +
							my_x + " " +
							my_y + " " +
							my_z + " " +
							my_rotx + " " +
							my_roty + " " +
							my_rotz + " " +
							my_vx + " " +
							my_vy + " " +
							my_vz + " " +
							my_avx + " " +
							my_avy + " " +
							my_avz + " " +
							my_supersonic + " " +
							my_throttle + " " +
							my_steer + " " +
							my_pitch + " " +
							my_yaw + " " +
							my_roll + " " +
							my_jump + " " +
							my_activateboost + " " +
							my_handbrake + " " +
							my_jumped + " " +
							my_boostamount + " " +
							opponent0_steamid + " " +
							opponent0_mmr + " " +
							opponent0_score + " " +
							opponent0_x + " " +
							opponent0_y + " " +
							opponent0_z + " " +
							opponent0_rotx + " " +
							opponent0_roty + " " +
							opponent0_rotz + " " +
							opponent0_vx + " " +
							opponent0_vy + " " +
							opponent0_vz + " " +
							opponent0_avx + " " +
							opponent0_avy + " " +
							opponent0_avz + " " +
							opponent0_supersonic + " " +
							opponent0_throttle + " " +
							opponent0_steer + " " +
							opponent0_pitch + " " +
							opponent0_yaw + " " +
							opponent0_roll + " " +
							opponent0_jump + " " +
							opponent0_activateboost + " " +
							opponent0_handbrake + " " +
							opponent0_jumped + " " +
							opponent0_boostamount + " " +
							ball_x + " " +
							ball_y + " " +
							ball_z + " " +
							ball_vx + " " +
							ball_vy + " " +
							ball_vz + " " +
							ball_avx + " " +
							ball_avy + " " +
							ball_avz + " " +
							my_ball_touches + " " +
							opponent0_ball_touches + " " +
							game_countdown_str + "\n";

						if (not this->data_file.is_open())
						{
							this->data_file.open(this->filepath + to_string(this->filecount) + ".txt", ios::app);
						}
						this->data_file << data_line;

						//getline(cin, data_line);
						//data_line = space2newline(data_line);
						//cvarManager->log(data_line);
					}
				}
			}
			else
			{
				if (this->data_file.is_open())
				{
					this->data_file.close();
				}
			}


			//gameWrapper->SetTimeout(bind(&nn_data_plugin::nn_loop, this), 0.01f);
		}
	}
}