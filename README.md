# Warning
This code is not being maintained and is provided for educational purposes only.

# Overview
This project is an attempt to create a Bluetooth LE mesh network that self organizes, and can do so quickly so that the network can pop into existence at specific times. This is NOT an implementation of the BLE mesh standard, which has limitations that I didn't want. In particular, that standard only allows leaf nodes to go to sleep and periodically report in. I wanted the entire mesh network to be able to do that. This project does exactly that and allows the sensors to quickly report their data and go back to sleep without the need for always on nodes in the system other than hub nodes at the edge.

While the code works and can be used to for an adhoc mesh network, I found that the connections were difficult to make and the BLE implementation in the ESP32 (DF Robot Firebeetle, to be specific) only worked over a distance of up to 10 feet, which isn't long enough to cover the area required.

I encourage anyone interested to take a look and see if there's a better way than I have done it. If the connections could be made in a few seconds and the distance between nodes could be up to 30 feet (10 meters) then it would work well enough to be of use.
