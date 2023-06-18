#include <conio.h>
#include "MmoTcpFighterServer.h"

int main(void)
{
	int key;
	bool isRunning = true;

	MmoTcpFighterServer* tcpfighterServer = new MmoTcpFighterServer(11601);
	tcpfighterServer->Start();

	while (isRunning)
	{
		tcpfighterServer->Monitoring();

		if (_kbhit())
		{
			key = _getch();
			if (key == 'Q' || key == 'q')
			{
				isRunning = false;
			}
		}
		Sleep(0);
	}
	delete tcpfighterServer;
	
	return 0;
}