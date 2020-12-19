This is a Bakkes Mod plugin which, when built as a dll, is used to automatically collect and upload Rocket League game data which can be used to train a neural network. The dll is made available in another repository: https://github.com/opalr/nn_data_plugin (this repo also shows what type of data is available).

This project can also take a path to a neural network model and control the vehicle using this model.

There are optional paramters in nn_dat_plugin/nn_data.h:
- enable_bot: Determines if the neural network model will be used to determine control values (mainly used for printing the values without using them for control).
- enable_bot_control: Determines if the control values should be used to control the vehicle (using vigem).
- enable_data_recording: Determines if data will be recorded from online matches and uploaded to the shared dropbox.
