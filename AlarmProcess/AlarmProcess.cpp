/*
*	Programa do Sistema de Dessulfura��o - Tabralho Pr�tico
*
*	AUTOMA��O EM TEMPO REAL - ELT012
*
*	Aluno: Gustavo Santana Cavassani
*
*	Professor: Luiz T. S. Mendes
*
*	PROCESSO DE EXIBI��O DE ALARMES
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

// Declara��o de objetos de sincroniza��o
HANDLE hEscEvent, hEventA, hMailslot;
HANDLE hMailslotEvent;			// Evento para sincroniza��o de entrada de dados no mailslot
HANDLE hMailslotCriadoEvent;	// Evento para sinaliza��o da cria��o do mailslot

// Declara��o de uma struct para representar os estados das threads (FALSE -> Desbloqueado / TRUE -> Bloqueado)
struct threadState {
	BOOL BLOCKED;
};

threadState AlarmMState;

// Declara��o de struct para armazenar parametros das mensagens de alarme
struct alarmMsg {
	int NSEQ;
	int ID;
	SYSTEMTIME TIME;
};

// Declara��o de fun��es
string Alarm_Format(alarmMsg& Alarm);
string textoAlarme(alarmMsg& Alarm);

BOOL bStatus;

int main() {
	int countOfMsgs = 0;	// Contador para contar quantas mensagens de alarme chegaram com a thread bloqueada
	alarmMsg AlarmBuffer;
	DWORD dwBytesLidos;

	setlocale(LC_ALL, "");
	printf("Processo de monitoramento de alarmes 'AlarmProcess' em execu��o.\n"); // Apenas para etapa 1, remover na etapa 2

	AlarmMState.BLOCKED = FALSE;	// Inicia desbloqueado
	printf("Estado Inicial: Desbloqueado.\n\n");

	//----Abertura dos Eventos----
	hEscEvent = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Evento_ESC");
	CheckForError(hEscEvent);

	hEventA = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Evento_a");
	CheckForError(hEventA);

	hMailslotCriadoEvent = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Evento_MailslotCriado");
	CheckForError(hMailslotCriadoEvent);

	hMailslotEvent = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Evento_Mailslot");
	CheckForError(hMailslotEvent);

	//----Cria��o do Mailslot----
	hMailslot = CreateMailslot(
		"\\\\.\\mailslot\\MyMailslot",
		0,
		MAILSLOT_WAIT_FOREVER,
		NULL);
	CheckForError(hMailslot != INVALID_HANDLE_VALUE);

	// Sinaliza que mailslot est� pronto para receber mensagens
	SetEvent(hMailslotCriadoEvent);

	// Recebimento dos eventos
	HANDLE arrayofObjects[3] = { hEscEvent, hEventA, hMailslotEvent };
	int nTipoEvento;
	DWORD dwRet;

	do {
		if (AlarmMState.BLOCKED == FALSE) {
			if (countOfMsgs > 0) {
				for (int i = 0; i < countOfMsgs; i++) {
					bStatus = ReadFile(hMailslot, &AlarmBuffer, sizeof(AlarmBuffer), &dwBytesLidos, NULL);
					CheckForError(bStatus);
					cout << Alarm_Format(AlarmBuffer) << endl;
				}
				countOfMsgs = 0;
			}
			dwRet = WaitForMultipleObjects(3, arrayofObjects, FALSE, INFINITE);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 0) {
				break; // Evento ESC
			}
			else if (nTipoEvento == 1) {
				printf("Evento de tecla a - Bloqueio thread 'AlarmProcess'.\n");
				AlarmMState.BLOCKED = TRUE;
				printf("Estado 'AlarmProcess': Bloqueada.\n");
			}
			else if (nTipoEvento == 2) {
				bStatus = ReadFile(hMailslot, &AlarmBuffer, sizeof(AlarmBuffer), &dwBytesLidos, NULL);
				CheckForError(bStatus);

				cout << Alarm_Format(AlarmBuffer) << endl;
			}
		}
		else {
			dwRet = WaitForMultipleObjects(3, arrayofObjects, FALSE, INFINITE);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 0) {
				break; // Evento ESC
			}
			else if (nTipoEvento == 1) {
				printf("Evento de tecla a - Desbloqueio thread 'AlarmProcess'.\n");
				AlarmMState.BLOCKED = FALSE;
				printf("Estado 'AlarmProcess': Desbloqueada.\n");
			}
			else if (nTipoEvento == 2) {
				countOfMsgs++;
			}
		}
	} while (nTipoEvento != 0);

	// Fechando os handles
	bStatus = CloseHandle(hEscEvent);
	CheckForError(bStatus);

	bStatus = CloseHandle(hEventA);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMailslot);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMailslotEvent);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMailslotCriadoEvent);
	CheckForError(bStatus);

	printf("Encerrando thread 'AlarmProcess'.\nDigite qualquer tecla para fechar a janela.\n");
	_getch();

	return EXIT_SUCCESS;
}

string Alarm_Format(alarmMsg& Alarm) {
	ostringstream alarmAux;
	string alarmReturn;

	if (Alarm.ID == 1) {	// 1 - Falha em CLP 1 | 2 - Falha em CLP 2
		alarmAux << setw(2) << setfill('0') << Alarm.TIME.wHour << ":" << setw(2) << setfill('0') << Alarm.TIME.wMinute << ":" << setw(2) << setfill('0') << Alarm.TIME.wSecond << " NSEQ: " << setw(5) << setfill('0') << Alarm.NSEQ << " FALHA DE HARDWARE CLP No. 1";
		alarmReturn = alarmAux.str();
		return alarmReturn;
	}
	else  if (Alarm.ID == 2) {
		alarmAux << setw(2) << setfill('0') << Alarm.TIME.wHour << ":" << setw(2) << setfill('0') << Alarm.TIME.wMinute << ":" << setw(2) << setfill('0') << Alarm.TIME.wSecond << " NSEQ: " << setw(5) << setfill('0') << Alarm.NSEQ << " FALHA DE HARDWARE CLP No. 2";
		alarmReturn = alarmAux.str();
		return alarmReturn;
	}
	else {
		alarmAux << setw(2) << setfill('0') << Alarm.TIME.wHour << ":" << setw(2) << setfill('0') << Alarm.TIME.wMinute << ":" << setw(2) << setfill('0') << Alarm.TIME.wSecond << " NSEQ: " << setw(5) << setfill('0') << Alarm.NSEQ << " ID: " << setw(2) << setfill('0') << Alarm.ID << " " << textoAlarme(Alarm);
		alarmReturn = alarmAux.str();
		return alarmReturn;
	}
}

string textoAlarme(alarmMsg& Alarm) {
	if (Alarm.ID > 0 && Alarm.ID <= 10) {
		string texto = "Temperatura cr�tica atingida no reator de dessulfura��o.";
		return texto;
	}
	else if (Alarm.ID > 10 && Alarm.ID <= 20) {
		string texto = "Detectado vazamento de g�s sulf�drico.";
		return texto;
	}
	else if (Alarm.ID > 20 && Alarm.ID <= 30) {
		string texto = "Press�o fora dos limites seguros no reator de dessulfura��o.";
		return texto;
	}
	else if (Alarm.ID > 30 && Alarm.ID <= 40) {
		string texto = "Sistema de inje��o de reagente inoperante.";
		return texto;
	}
	else if (Alarm.ID > 40 && Alarm.ID <= 50) {
		string texto = "Baixo fluxo de g�s detectado no sistema.";
		return texto;
	}
	else if (Alarm.ID > 50 && Alarm.ID <= 60) {
		string texto = "N�veis elevados de subprodutos n�o desejados detectados.";
		return texto;
	}
	else if (Alarm.ID > 60 && Alarm.ID <= 70) {
		string texto = "Sistema de monitoramento ambiental offline.";
		return texto;
	}
	else if (Alarm.ID > 70 && Alarm.ID <= 80) {
		string texto = "Sobrecarga el�trica detectada no sistema de dessulfura��o.";
		return texto;
	}
	else if (Alarm.ID > 80 && Alarm.ID <= 90) {
		string texto = "Sinais de corros�o detectados em componentes do sistema.";
		return texto;
	}
	else {
		string texto = "N�veis de efluentes fora dos padr�es de seguran�a.";
		return texto;
	}
}