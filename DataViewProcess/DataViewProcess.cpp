/*
*	Programa do Sistema de Dessulfura��o - Tabralho Pr�tico
*
*	AUTOMA��O EM TEMPO REAL - ELT012
*
*	Aluno: Gustavo Santana Cavassani
*
*	Professor: Luiz T. S. Mendes
*
*	PROCESSO DE EXIBI��O DE DADOS
*/

#define WIN32_LEAN_AND_MEAN 
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1

using namespace std;

#include <locale>
#include <sstream>
#include <iomanip>
#include <string>
#include <iostream>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>			// _beginthreadex() e _endthreadex()
#include <conio.h>				// _getch

#define	_CHECKERROR		1		// Ativa fun��o CheckForError
#include "CheckForError.h"
// Tentei usar a fun��o, mas n�o funcionava dizendo que o tipo const char* � incompat�vel com LPSTR, n�o encontrei uma forma de corrigir, ent�o usei GetLastError()

// Casting para terceiro e sexto par�metros da fun��o _beginthreadex
typedef unsigned (WINAPI* CAST_FUNCTION)(LPVOID);
typedef unsigned* CAST_LPDWORD;

// Constantes definindo as teclas que ser�o digitadas e seus endere�os em hexadecimal
#define	ESC			0x1B		// Tecla para encerrar o programa
#define TECLA_1		0x31		// Tecla para notifica��o de leitura do CLP #1
#define TECLA_2		0x32		// Tecla para notifica��o de leitura do CLP #2
#define TECLA_m		0x6D		// Tecla para notifica��o de leitura do CLP de Alarmes
#define TECLA_r		0x72		// Tecla para notifica��o de retirada de mensagens
#define TECLA_p		0x70		// Tecla para notifica��o de exibi��o de dados do processo
#define TECLA_a		0x61		// Tecla para notifica��o de exibi��o de alarmes

HANDLE hEscEvent, hEventP;
HANDLE hMappedSection;			// Arquivo mapeado em mem�ria
HANDLE hMapFileWriteEvent;		// Evento de sinaliza��o de escrita no arquivo mapeado
HANDLE hMapFileReadEvent;		// Evento de sinaliza��o de leitura no arquivo mapeado

// Declara��o de struct para armazenar parametros das mensagens de dados do processo
struct dataMsg {
	int NSEQ;
	int ID;
	int DIAG;
	float P_INT;
	float P_INJ;
	float TEMP;
	SYSTEMTIME TIME;
};

dataMsg msgSize;	// Instanciando uma vari�vel do tipo da struct apenas para passar o seu sizeof() na fun��o de CreateFileMapping

// Struct para armazenar o estado da thread
struct threadState {
	BOOL BLOCKED;
};

threadState DataViewState;

BOOL bStatus;

int main() {
	dataMsg* lpImage;

	setlocale(LC_ALL, "");
	printf("Processo de exibi��o de dados 'DataViewProcess' em execu��o.\n"); // Apenas para etapa 1, remover na etapa 2

	DataViewState.BLOCKED = FALSE;	// Inicia desbloqueado
	printf("Estado Inicial: Desbloqueado.\n\n");

	// Abertura do arquivo mapeado em mem�ria para lista 2
	hMappedSection = OpenFileMapping(FILE_MAP_ALL_ACCESS,
		FALSE,
		"ArquivoMapeado");
	CheckForError(hMappedSection);

	lpImage = (dataMsg*)MapViewOfFile(hMappedSection,
		FILE_MAP_WRITE,
		0,
		0,
		sizeof(msgSize) * 50);
	CheckForError(lpImage);

	//----Abertura dos Eventos----
	hEscEvent = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Evento_ESC");
	CheckForError(hEscEvent);

	hEventP = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Evento_p");
	CheckForError(hEventP);

	hMapFileWriteEvent = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Evento_MapFileWrite");
	CheckForError(hMapFileWriteEvent);

	hMapFileReadEvent = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Evento_MapFileRead");
	CheckForError(hMapFileReadEvent);

	// Recebimento dos eventos
	HANDLE firstArrayofObjects[2] = { hEscEvent, hEventP };
	HANDLE secondArrayofObjects[3] = { hEscEvent, hEventP, hMapFileWriteEvent };
	int nTipoEvento;
	DWORD dwRet;

	do {
		if (DataViewState.BLOCKED == FALSE) {
			dwRet = WaitForMultipleObjects(3, secondArrayofObjects, FALSE, INFINITE);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 1) {
				printf("Evento de tecla p - Bloqueio thread 'DataViewProcess'.\n");
				DataViewState.BLOCKED = TRUE;
				printf("Estado 'DataViewProcess': Bloqueada.\n");
			}
			else if (nTipoEvento == 2) {
				ostringstream dataMsgAux;
				string dataMsg;
				dataMsgAux << setw(2) << setfill('0') << lpImage->TIME.wHour << ":" << setw(2) << setfill('0') << lpImage->TIME.wMinute << ":" << setw(2) << setfill('0') << lpImage->TIME.wSecond << " NSEQ: " << setw(5) << setfill('0') << lpImage->NSEQ << " PR INT: " << setw(4) << setfill('0') << lpImage->P_INT << " PR N2: " << setw(4) << setfill('0') << lpImage->P_INJ << " TEMP: " << setw(4) << setfill('0') << lpImage->TEMP;
				dataMsg = dataMsgAux.str();
				cout << dataMsg << endl;
				SetEvent(hMapFileReadEvent);
			}
		}
		else {
			dwRet = WaitForMultipleObjects(2, firstArrayofObjects, FALSE, INFINITE);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 1) {
				printf("Evento de tecla p - Desbloqueio thread 'DataViewProcess'.\n");
				DataViewState.BLOCKED = FALSE;
				printf("Estado 'DataViewProcess': Desbloqueada.\n");
			}
		}
	} while (nTipoEvento != 0);

	// Apaga o mapeamento em mem�ria
	bStatus = UnmapViewOfFile(lpImage);
	CheckForError(bStatus);

	// Fechando os handles
	bStatus = CloseHandle(hEscEvent);
	CheckForError(bStatus);

	bStatus = CloseHandle(hEventP);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMappedSection);
	CheckForError(bStatus);

	printf("Encerrando thread 'DataViewProcess'.\nDigite qualquer tecla para fechar a janela.\n");
	_getch();

	return EXIT_SUCCESS;

}