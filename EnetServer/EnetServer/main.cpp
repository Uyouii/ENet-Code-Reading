#include"enet/enet.h"
#include<cstdlib>
#include<cstdio>

#define HOSTADDRESS "10.242.3.221"

ENetHost* createServer() {
	ENetAddress address;
	ENetHost * server;

	enet_address_set_host(&address, HOSTADDRESS);
	address.port = 1234;

	server = enet_host_create(
		&address /* the address to bind the server host to */,
		32      /* allow up to 32 clients and/or outgoing connections */,
		2      /* allow up to 2 channels to be used, 0 and 1 */,
		0      /* assume any amount of incoming bandwidth */,
		0      /* assume any amount of outgoing bandwidth */
	);

	if (server == NULL)
	{
		printf("An error occurred while initializing Server.\n");
		system("pause");
		exit(EXIT_FAILURE);
	}

	printf("Create Server Succellfully!\n");
	return server;
	//enet_host_destroy(server);
}


int main(int argc, char ** argv)
{
	if (enet_initialize() != 0)
	{
		printf("An error occurred while initializing ENet.\n");
		system("pause");
		exit(EXIT_FAILURE);
	}

	ENetHost* server = createServer();

	ENetEvent event;
	/* Wait up to 1000 milliseconds for an event. */
	while (enet_host_service(server, &event, 100) >= 0)
	{
		switch (event.type)
		{
		case ENET_EVENT_TYPE_CONNECT:
			printf("A new client connected from %x:%u.\n",
				event.peer->address.host,
				event.peer->address.port);
			/* Store any relevant client information here. */
			event.peer->data = "Client information";
			break;
		case ENET_EVENT_TYPE_RECEIVE:
			printf("A packet of length %u containing %s was received from %s on channel %u.\n",
				event.packet->dataLength,
				event.packet->data,
				event.peer->data,
				event.channelID);
			/* Clean up the packet now that we're done using it. */
			enet_packet_destroy(event.packet);

			break;

		case ENET_EVENT_TYPE_DISCONNECT:
			printf("%s disconnected.\n", event.peer->data);
			/* Reset the peer's client information. */
			event.peer->data = NULL;
			break;
		}
	}

	enet_host_destroy(server);


	atexit(enet_deinitialize);
	system("pause");

}