/*
*	Programa do Sistema de Dessulfuração - Tabralho Prático
*
*	AUTOMAÇÃO EM TEMPO REAL - ELT012
*
*	Aluno: Gustavo Santana Cavassani
*
*	Professor: Luiz T. S. Mendes
*
*	PROCESSO PRINCIPAL
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

#define	_CHECKERROR		1		// Ativa função CheckForError
#include "CheckForError.h"

// Casting para terceiro e sexto parâmetros da função _beginthreadex
typedef unsigned (WINAPI* CAST_FUNCTION)(LPVOID);
typedef unsigned* CAST_LPDWORD;

// Constantes definindo as teclas que serão digitadas e seus endereços em hexadecimal
#define	ESC			0x1B		// Tecla para encerrar o programa
#define TECLA_1		0x31		// Tecla para notificação de leitura do CLP #1
#define TECLA_2		0x32		// Tecla para notificação de leitura do CLP #2
#define TECLA_m		0x6D		// Tecla para notificação de leitura do CLP de Alarmes
#define TECLA_r		0x72		// Tecla para notificação de retirada de mensagens
#define TECLA_p		0x70		// Tecla para notificação de exibição de dados do processo
#define TECLA_a		0x61		// Tecla para notificação de exibição de alarmes

// Declaração de objetos de sincronização
HANDLE hEscEvent, hMailslot;
HANDLE hNotificationEvents[6];
HANDLE hMutexforNSEQ;			// Mutex para exclusão mútua da variável NSEQ
HANDLE hSemforList1_IN;			// Semaforo para lógica de 'contagem' do acesso à lista circular 1 para depositar mensagens
HANDLE hSemforList1_OUT;		// Semaforo para lógica de 'contagem' do acesso à lista circular 1 para retirar mensagens
HANDLE hSemforList2_IN;			// Semaforo para lógica de 'contagem' do acesso à lista circular 2 para depositar mensagens
HANDLE hSemforList2_OUT;		// Semaforo para lógica de 'contagem' do acesso à lista circular 2 para retirar mensagens
HANDLE hMutexforList1;			// Mutex para exclusão mútua da atualização da variável de posição livre da lista circular 1
HANDLE hMutexforMailslot;		// Mutex para exclusão mútua do mailslot
HANDLE hMailslotEvent;			// Evento para sincronização de entrada de dados no mailslot
HANDLE hMailslotCriadoEvent;	// Evento para sinalização da criação do mailslot
HANDLE hMappedSection;			// Arquivo mapeado em memória
HANDLE hMapFileWriteEvent;		// Evento de sinalização de escrita no arquivo mapeado
HANDLE hMapFileReadEvent;		// Evento de sinalização de leitura no arquivo mapeado

// Declaração das funções das threads
DWORD WINAPI CLP1_Read();
DWORD WINAPI CLP2_Read();
DWORD WINAPI Alarm_Monitor();
DWORD WINAPI Messages_Removal();

// Declaração de uma struct para representar os estados das threads (FALSE -> Desbloqueado / TRUE -> Bloqueado)
struct threadState {
	BOOL BLOCKED;
};

threadState CLP1State, CLP2State, AlarmMState, MsgRmvState;

struct previousValue {
	int alarmid;
	int diag;
	int p_int;
	int p_inj;
	int temp;
};

previousValue Messages;

// Declaração de struct para armazenar parametros das mensagens de alarme
struct alarmMsg {
	int NSEQ;
	int ID;
	SYSTEMTIME TIME;
};

// Declaração de struct para armazenar parametros das mensagens de dados do processo
struct dataMsg {
	int NSEQ;
	int ID;
	int DIAG;
	float P_INT;
	float P_INJ;
	float TEMP;
	SYSTEMTIME TIME;
};

dataMsg msgSize;	// Instanciando uma variável do tipo da struct apenas para passar o seu sizeof() na função de CreateFileMapping

// Declaração de funções para 'formatar' as mensagens
int alarmID_Format();
int DIAG_Format();
float P_INT_Format();
float P_INJ_Format();
float TEMP_Format();
void Message_Creation(dataMsg& Message, int ID, int NSEQAux);
void Alarm_Creation(alarmMsg& Alarm, int NSEQAux, int ID);

// Declaração de variáveis globais
BOOL bStatus;
DWORD dwRet;
DWORD dwBytesEnviados;
int NSEQ = 0;
int NSEQ_Alarm = 0;
dataMsg listaCircular1[100];	// Lista circular 1
int freePositionList1 = 0;		// Variável para armazenar a última posição livre da lista circular a ser adicionada mensagem
int ocupPositionList1 = 0;		// Variável para armazenar a última posição ocupada da lista circular a qual foi retirada mensagem
dataMsg listaCircular2[50];		// Lista circular 2
int freePositionList2 = 0;		// Variável para armazenar a última posição livre da lista circular a ser adicionada mensagem
int ocupPositionList2 = 0;		// Variável para armazenar a última posição ocupada da lista circular a qual foi retirada mensagem
dataMsg* lpImage;				// Apontador para o arquivo mapeado

int main() {
	setlocale(LC_ALL, "");

	CLP1State.BLOCKED = FALSE;		// Inicia desbloqueada
	CLP2State.BLOCKED = FALSE;		// Inicia desbloqueada
	AlarmMState.BLOCKED = FALSE;	// Inicia desbloqueada
	MsgRmvState.BLOCKED = FALSE;	// Inicia desbloqueada

	printf("Digite uma tecla entre 1, 2, m, r, p, a ou ESC:\n");
	printf(" 1 -> Bloqueio/Desbloqueio leitura do CLP 1\n");
	printf(" 2 -> Bloqueio/Desbloqueio leitura do CLP 2\n");
	printf(" m -> Bloqueio/Desbloqueio monitoramento de alarmes\n");
	printf(" r -> Bloqueio/Desbloqueio retirada de mensagens\n");
	printf(" p -> Bloqueio/Desbloqueio exibição de dados do processo\n");
	printf(" a -> Bloqueio/Desbloqueio exibição de alarmes\n");
	printf(" ESC -> Encerramento da execução\n");

	Messages.alarmid = 0;
	Messages.diag = 0;
	Messages.p_int = 0;
	Messages.p_inj = 0;
	Messages.temp = 0;

	int nTecla;

	HANDLE hThreads[4];
	DWORD dwCLP1Read, dwCLP2Read, dwAlarmMonitor, dwMessagesRemoval, dwExitCode;

	STARTUPINFO siDataViewProcess;				// Estrutura necessária para criação de um processo (Informações de Inicialização)
	PROCESS_INFORMATION piDataViewProcess;		// Estrutura necessária para criação de um processo (Informações de Dados do Processo)
	ZeroMemory(&siDataViewProcess, sizeof(siDataViewProcess));
	siDataViewProcess.cb = sizeof(siDataViewProcess);
	ZeroMemory(&piDataViewProcess, sizeof(piDataViewProcess));

	STARTUPINFO siAlarmViewProcess;				// Estrutura necessária para criação de um processo (Informações de Inicialização)
	PROCESS_INFORMATION piAlarmViewProcess;		// Estrutura necessária para criação de um processo (Informações de Dados do Processo)
	ZeroMemory(&siAlarmViewProcess, sizeof(siAlarmViewProcess));
	siAlarmViewProcess.cb = sizeof(siAlarmViewProcess);
	ZeroMemory(&piAlarmViewProcess, sizeof(piAlarmViewProcess));

	// Criação do arquivo mapeado em memória para lista 2
	hMappedSection = CreateFileMapping(INVALID_HANDLE_VALUE,
		NULL,
		PAGE_READWRITE,
		0,
		sizeof(msgSize) * 50,
		"ArquivoMapeado");
	CheckForError(hMappedSection);

	lpImage = (dataMsg*)MapViewOfFile(hMappedSection,
		FILE_MAP_WRITE,
		0,
		0,
		sizeof(msgSize) * 50);
	CheckForError(lpImage);

	//----Criação dos eventos----
	// 1.Evento da tecla '1':
	hNotificationEvents[0] = CreateEvent(NULL, FALSE, FALSE, "Evento_1");
	CheckForError(hNotificationEvents[0]);

	// 2.Evento da tecla '2':
	hNotificationEvents[1] = CreateEvent(NULL, FALSE, FALSE, "Evento_2");
	CheckForError(hNotificationEvents[1]);

	// 3.Evento da tecla 'm':
	hNotificationEvents[2] = CreateEvent(NULL, FALSE, FALSE, "Evento_m");
	CheckForError(hNotificationEvents[2]);

	// 4.Evento da tecla 'r':
	hNotificationEvents[3] = CreateEvent(NULL, FALSE, FALSE, "Evento_r");
	CheckForError(hNotificationEvents[3]);

	// 5.Evento da tecla 'p':
	hNotificationEvents[4] = CreateEvent(NULL, FALSE, FALSE, "Evento_p");
	CheckForError(hNotificationEvents[4]);

	// 6.Evento da tecla 'a':
	hNotificationEvents[5] = CreateEvent(NULL, FALSE, FALSE, "Evento_a");
	CheckForError(hNotificationEvents[5]);

	// 7.Evento da tecla 'ESC'(Declarada fora do vetor de eventos de notificação, pois esse deve ser de reset manual para notificar todos): 
	hEscEvent = CreateEvent(NULL, TRUE, FALSE, "Evento_ESC");
	CheckForError(hEscEvent);

	// 8.Evento de que mailslot na exibição de alarmes foi criado:
	hMailslotCriadoEvent = CreateEvent(NULL, FALSE, FALSE, "Evento_MailslotCriado");
	CheckForError(hMailslotCriadoEvent);

	// 9.Evento de sinalização de mailslot (Envio de mensagens)
	hMailslotEvent = CreateEvent(NULL, FALSE, FALSE, "Evento_Mailslot");
	CheckForError(hMailslotEvent);

	// 10.Eventos de sinalizção de leitura e escrita no arquivo mapeado em memória
	hMapFileWriteEvent = CreateEvent(NULL, FALSE, FALSE, "Evento_MapFileWrite");
	CheckForError(hMapFileWriteEvent);

	hMapFileReadEvent = CreateEvent(NULL, FALSE, FALSE, "Evento_MapFileRead");
	CheckForError(hMapFileReadEvent);

	//----Criação de Mutexes e Semaforos----
	hMutexforNSEQ = CreateMutex(NULL, FALSE, "MutexforNSEQ");
	CheckForError(hMutexforNSEQ);
	hSemforList1_IN = CreateSemaphore(NULL, 100, 100, "SemaphoreList1_IN");
	CheckForError(hSemforList1_IN);
	hSemforList1_OUT = CreateSemaphore(NULL, 0, 100, "SemaphoreList1_OUT");
	CheckForError(hSemforList1_OUT);
	hSemforList2_IN = CreateSemaphore(NULL, 50, 50, "SemaphoreList2_IN");
	CheckForError(hSemforList2_IN);
	hSemforList2_OUT = CreateSemaphore(NULL, 0, 50, "SemaphoreList2_OUT");
	CheckForError(hSemforList2_OUT);
	//Foram criados dois semáforos 'inversos', para sinalizar e armazenar a entrada e saída de mensagens da lista circular
	hMutexforList1 = CreateMutex(NULL, FALSE, "MutexforList1");
	CheckForError(hMutexforList1);
	hMutexforMailslot = CreateMutex(NULL, FALSE, "MutexforMailslot");
	CheckForError(hMutexforMailslot);

	//----Criação dos processos----
	// 1.Processo que engloba a exibição de dados de processo - DataViewProcess:
	bStatus = CreateProcess("..\\MainProcess\\x64\\Debug\\DataViewProcess.exe",
		NULL,
		NULL,
		NULL,
		FALSE,
		CREATE_NEW_CONSOLE,
		NULL,
		NULL,
		&siDataViewProcess,
		&piDataViewProcess);
	CheckForError(bStatus);

	// 2.Processo que engloba a exibição de alarmes - AlarmProcess:

	bStatus = CreateProcess("..\\MainProcess\\x64\\Debug\\AlarmProcess.exe",
		NULL,
		NULL,
		NULL,
		FALSE,
		CREATE_NEW_CONSOLE,
		NULL,
		NULL,
		&siAlarmViewProcess,
		&piAlarmViewProcess);
	CheckForError(bStatus);

	//----Criação das threads----
	// 1.Thread de leitura do CLP1 - ThreadCLP1Read:
	hThreads[0] = (HANDLE)_beginthreadex(NULL,
		0,
		(CAST_FUNCTION)CLP1_Read,
		NULL,
		0,
		(CAST_LPDWORD)&dwCLP1Read
	);
	CheckForError(hThreads[0]);

	// 2.Thread de leitura do CLP2 - ThreadCLP2Read:
	hThreads[1] = (HANDLE)_beginthreadex(NULL,
		0,
		(CAST_FUNCTION)CLP2_Read,
		NULL,
		0,
		(CAST_LPDWORD)&dwCLP2Read
	);
	CheckForError(hThreads[1]);

	// 3.Thread de leitura do CLP de monitoramento de alarmes - ThreadAlarmMonitor:
	hThreads[2] = (HANDLE)_beginthreadex(NULL,
		0,
		(CAST_FUNCTION)Alarm_Monitor,
		NULL,
		0,
		(CAST_LPDWORD)&dwAlarmMonitor
	);
	CheckForError(hThreads[2]);

	// 4.Thread de retirada de mensagens - ThreadMessagesRemoval:
	hThreads[3] = (HANDLE)_beginthreadex(NULL,
		0,
		(CAST_FUNCTION)Messages_Removal,
		NULL,
		0,
		(CAST_LPDWORD)&dwMessagesRemoval
	);
	CheckForError(hThreads[3]);

	// Wait pelo evento que sinaliza que o mailslot foi criado
	WaitForSingleObject(hMailslotCriadoEvent, INFINITE);

	// Criação do arquivo mailslot
	hMailslot = CreateFile("\\\\.\\mailslot\\MyMailslot",
		GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	CheckForError(hMailslot != INVALID_HANDLE_VALUE);

	// Lógica de tratamento do teclado (Não criei uma thread secundária para isso, fiz uso da primária como a thread de tratamento de teclado)
	do {
		nTecla = _getch();
		if (nTecla == TECLA_1) {
			bStatus = SetEvent(hNotificationEvents[0]);
			CheckForError(bStatus);
			printf("Evento 'Tecla 1' setado!\n"); 
		}
		else if (nTecla == TECLA_2) {
			bStatus = SetEvent(hNotificationEvents[1]);
			CheckForError(bStatus);
			printf("Evento 'Tecla 2' setado!\n"); 
		}
		else if (nTecla == TECLA_m) {
			bStatus = SetEvent(hNotificationEvents[2]);
			CheckForError(bStatus);
			printf("Evento 'Tecla m' setado!\n");
		}
		else if (nTecla == TECLA_r) {
			bStatus = SetEvent(hNotificationEvents[3]);
			CheckForError(bStatus);
			printf("Evento 'Tecla r' setado!\n"); 
		}
		else if (nTecla == TECLA_p) {
			bStatus = SetEvent(hNotificationEvents[4]);
			CheckForError(bStatus);
			printf("Evento 'Tecla p' setado!\n"); 
		}
		else if (nTecla == TECLA_a) {
			bStatus = SetEvent(hNotificationEvents[5]);
			CheckForError(bStatus);
			printf("Evento 'Tecla a' setado!\n"); 
		}
	} while (nTecla != ESC);

	// Set do ESC feito fora da lógica de tratamento de teclas, pois se for digitado ESC, sai do loop e finaliza os processos
	bStatus = SetEvent(hEscEvent);
	CheckForError(bStatus);
	printf("Evento 'Tecla ESC' setado!\n"); 

	// Esperando todas threads encerrarem
	dwRet = WaitForMultipleObjects(4, hThreads, TRUE, INFINITE);
	if (dwRet != WAIT_OBJECT_0) printf("Encerramento das threads falhou (%d).\n", GetLastError());

	// Apaga o mapeamento em memória
	bStatus = UnmapViewOfFile(lpImage);
	CheckForError(bStatus);

	//----Fechando os handles----
	for (int i = 0; i < 4; i++) {
		if (i == 0) {
			GetExitCodeThread(hThreads[i], &dwExitCode);
			printf("Thread 'CLP1_Read' terminou (%d).\n", dwExitCode);
			CloseHandle(hThreads[i]);	// apaga referência ao objeto
		}
		else if (i == 1) {
			GetExitCodeThread(hThreads[i], &dwExitCode);
			printf("Thread 'CLP2_Read' terminou (%d).\n", dwExitCode);
			CloseHandle(hThreads[i]);	// apaga referência ao objeto
		}
		else if (i == 2) {
			GetExitCodeThread(hThreads[i], &dwExitCode);
			printf("Thread 'Alarm_Monitor' terminou (%d).\n", dwExitCode);
			CloseHandle(hThreads[i]);	// apaga referência ao objeto
		}
		else if (i == 3) {
			GetExitCodeThread(hThreads[i], &dwExitCode);
			printf("Thread 'Messages_Removal' terminou (%d).\n", dwExitCode);
			CloseHandle(hThreads[i]);	// apaga referência ao objeto
		}
	}

	for (int i = 0; i < 6; i++) {
		bStatus = CloseHandle(hNotificationEvents[i]);
		CheckForError(bStatus);
	}

	bStatus = CloseHandle(hEscEvent);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMailslot);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMailslotEvent);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMailslotCriadoEvent);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMutexforNSEQ);
	CheckForError(bStatus);

	bStatus = CloseHandle(hSemforList1_IN);
	CheckForError(bStatus);

	bStatus = CloseHandle(hSemforList1_OUT);
	CheckForError(bStatus);

	bStatus = CloseHandle(hSemforList2_IN);
	CheckForError(bStatus);

	bStatus = CloseHandle(hSemforList2_OUT);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMutexforList1);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMutexforMailslot);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMappedSection);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMapFileWriteEvent);
	CheckForError(bStatus);

	bStatus = CloseHandle(hMapFileReadEvent);
	CheckForError(bStatus);

	return EXIT_SUCCESS;
}//main

DWORD WINAPI CLP1_Read() {
	HANDLE firstArrayofObjects[3] = { hEscEvent, hNotificationEvents[0], hMutexforNSEQ };
	HANDLE secondArrayofObjects[3] = { hEscEvent, hNotificationEvents[0], hSemforList1_IN };
	HANDLE thirdArrayofObjects[3] = { hEscEvent, hNotificationEvents[0], hMutexforList1 };
	HANDLE fourthArrayofObjects[2] = { hEscEvent, hNotificationEvents[0] };
	DWORD dwRet;
	int nTipoEvento, NSEQAux;
	dataMsg msgCreated;

	printf("Estado 'CLP1_Read': Desbloqueada.\n");

	do {
		if (CLP1State.BLOCKED == FALSE) {
			// Wait utilizado para a "temporização" da thread clp, na qual a mesma faz uma "espera" de 500ms apenas para gerar a temporização de leitura dos CLPS 
			dwRet = WaitForSingleObject(hEscEvent, 500);
			if (dwRet == WAIT_OBJECT_0) {
				break; // Ocorreu o evento de ESC
			}
			else if (dwRet == WAIT_TIMEOUT) {
				// Primeiro 'wait' para exclusão mútua e definição do NSEQ
				dwRet = WaitForMultipleObjects(3, firstArrayofObjects, FALSE, INFINITE);
				nTipoEvento = dwRet - WAIT_OBJECT_0;
				if (nTipoEvento == 0) {
					break;
				}
				else if (nTipoEvento == 1) {
					printf("Evento de tecla 1 - Bloqueio thread 'CLP1_Read'.\n");
					CLP1State.BLOCKED = TRUE;
					printf("Estado 'CLP1_Read': Bloqueada.\n");
				}
				else if (nTipoEvento == 2) {
					NSEQAux = NSEQ;
					if (NSEQ == 99999) NSEQ = 0;
					else
						NSEQ++;

					bStatus = ReleaseMutex(hMutexforNSEQ);
					CheckForError(bStatus);
				}
				Message_Creation(msgCreated, 1, NSEQAux);

				// Segundo 'wait' para adição da mensagem à lista circular 1
				dwRet = WaitForMultipleObjects(3, secondArrayofObjects, FALSE, 5);
				// Tenta conquistar semaforo da lista circular 1, se passa de 5ms significa que a lista 1 está cheia e fica bloqueada esperando até ser retirada uma mensagem
				if (dwRet == WAIT_TIMEOUT) {
					if (MsgRmvState.BLOCKED == FALSE) {
						printf("Listas Circulares 1 e 2 Cheias!\n");
					}
					else {
						printf("Lista Circular 1 Cheia!\n");
					}
					dwRet = WaitForMultipleObjects(3, secondArrayofObjects, FALSE, INFINITE);
				}

				nTipoEvento = dwRet - WAIT_OBJECT_0;

				if (nTipoEvento == 0) {
					break;
				}
				else if (nTipoEvento == 1) {
					printf("Evento de tecla 1 - Bloqueio thread 'CLP1_Read'.\n");
					CLP1State.BLOCKED = TRUE;
					printf("Estado 'CLP1_Read': Bloqueada.\n");
				}
				else if (nTipoEvento == 2) {
					dwRet = WaitForMultipleObjects(3, thirdArrayofObjects, FALSE, INFINITE);	// Tenta conquistar o mutex para exclusão mútua de lista circular 1

					nTipoEvento = dwRet - WAIT_OBJECT_0;
					if (nTipoEvento == 0) {
						break;
					}
					else if (nTipoEvento == 1) {
						printf("Evento de tecla 1 - Bloqueio thread 'CLP1_Read'.\n");
						CLP1State.BLOCKED = TRUE;
						printf("Estado 'CLP1_Read': Bloqueada.\n");
					}
					else if (nTipoEvento == 2) {
						listaCircular1[freePositionList1] = msgCreated;

						freePositionList1 = (freePositionList1 + 1) % 100;

						bStatus = ReleaseMutex(hMutexforList1);
						CheckForError(bStatus);
						bStatus = ReleaseSemaphore(hSemforList1_OUT, 1, NULL);
						CheckForError(bStatus);
					}
				}
			}
		}
		else {
			// Espera de desbloqueio da thread
			dwRet = WaitForMultipleObjects(2, fourthArrayofObjects, FALSE, INFINITE);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 0) {
				break;
			}
			else if (nTipoEvento == 1) {
				printf("Evento de tecla 1 - Desbloqueio thread 'CLP1_Read'.\n");
				CLP1State.BLOCKED = FALSE;
				printf("Estado 'CLP1_Read': Desbloqueada.\n");
			}
		}
	} while (1);

	printf("Encerrando thread 'CLP1_Read'.\n");

	_endthreadex(0);

	return 0;
}

DWORD WINAPI CLP2_Read() {
	HANDLE firstArrayofObjects[3] = { hEscEvent, hNotificationEvents[1], hMutexforNSEQ };
	HANDLE secondArrayofObjects[3] = { hEscEvent, hNotificationEvents[1], hSemforList1_IN };
	HANDLE thirdArrayofObjects[3] = { hEscEvent, hNotificationEvents[1], hMutexforList1 };
	HANDLE fourthArrayofObjects[2] = { hEscEvent, hNotificationEvents[1] };
	DWORD dwRet;
	int nTipoEvento, NSEQAux;
	dataMsg msgCreated;

	printf("Estado 'CLP2_Read': Desbloqueada.\n");

	do {
		if (CLP2State.BLOCKED == FALSE) {
			// Wait utilizado para a "temporização" da thread clp, na qual a mesma faz uma "espera" de 500ms apenas para gerar a temporização de leitura dos CLPS 
			dwRet = WaitForSingleObject(hEscEvent, 500);
			if (dwRet == WAIT_OBJECT_0) {
				break; // Ocorreu o evento de ESC
			}
			else if (dwRet == WAIT_TIMEOUT) {
				// Primeiro 'wait' para exclusão mútua e definição do NSEQ
				dwRet = WaitForMultipleObjects(3, firstArrayofObjects, FALSE, INFINITE);

				nTipoEvento = dwRet - WAIT_OBJECT_0;
				if (nTipoEvento == 0) {
					break;
				}
				else if (nTipoEvento == 1) {
					printf("Evento de tecla 2 - Bloqueio thread 'CLP2_Read'.\n");
					CLP2State.BLOCKED = TRUE;
					printf("Estado 'CLP2_Read': Bloqueada.\n");
				}
				else if (nTipoEvento == 2) {
					NSEQAux = NSEQ;
					if (NSEQ == 99999) NSEQ = 0;
					else
						NSEQ++;

					bStatus = ReleaseMutex(hMutexforNSEQ);
					CheckForError(bStatus);
				}

				Message_Creation(msgCreated, 2, NSEQAux);

				// Segundo 'wait' para adição da mensagem à lista circular 1
				dwRet = WaitForMultipleObjects(3, secondArrayofObjects, FALSE, 5);

				// Tenta conquistar semaforo da lista circular 1, se passa de 5ms significa que a lista 1 está cheia e fica bloqueada esperando até ser retirada uma mensagem
				if (dwRet == WAIT_TIMEOUT) {
					if (MsgRmvState.BLOCKED == FALSE) {
						printf("Listas Circulares 1 e 2 Cheias!\n");
					}
					else {
						printf("Lista Circular 1 Cheia!\n");
					}
					dwRet = WaitForMultipleObjects(3, secondArrayofObjects, FALSE, INFINITE);
				}

				nTipoEvento = dwRet - WAIT_OBJECT_0;
				if (nTipoEvento == 0) {
					break;
				}
				else if (nTipoEvento == 1) {
					printf("Evento de tecla 2 - Bloqueio thread 'CLP2_Read'.\n");
					CLP2State.BLOCKED = TRUE;
					printf("Estado 'CLP2_Read': Bloqueada.\n");
				}
				else if (nTipoEvento == 2) {
					dwRet = WaitForMultipleObjects(3, thirdArrayofObjects, FALSE, INFINITE);	// Tenta conquistar o mutex para exclusão mútua de lista circular 1

					nTipoEvento = dwRet - WAIT_OBJECT_0;
					if (nTipoEvento == 0) {
						break;
					}
					else if (nTipoEvento == 1) {
						printf("Evento de tecla 2 - Bloqueio thread 'CLP2_Read'.\n");
						CLP2State.BLOCKED = TRUE;
						printf("Estado 'CLP2_Read': Bloqueada.\n");
					}
					else if (nTipoEvento == 2) {
						listaCircular1[freePositionList1] = msgCreated;

						freePositionList1 = (freePositionList1 + 1) % 100;

						bStatus = ReleaseMutex(hMutexforList1);
						CheckForError(bStatus);
						bStatus = ReleaseSemaphore(hSemforList1_OUT, 1, NULL);
						CheckForError(bStatus);
					}
				}
			}
		}
		else {
			// Espera de desbloqueio da thread
			dwRet = WaitForMultipleObjects(2, fourthArrayofObjects, FALSE, INFINITE);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 0) {
				break;
			}
			else if (nTipoEvento == 1) {
				printf("Evento de tecla 2 - Desbloqueio thread 'CLP2_Read'.\n");
				CLP2State.BLOCKED = FALSE;
				printf("Estado 'CLP2_Read': Desbloqueada.\n");
			}
		}
	} while (1);

	printf("Encerrando thread 'CLP2_Read'.\n");

	_endthreadex(0);

	return 0;
}

DWORD WINAPI Alarm_Monitor() {
	HANDLE firstArrayofObjects[2] = { hEscEvent, hNotificationEvents[2] };
	DWORD dwRet;
	int nTipoEvento, randomTimeout;
	alarmMsg Alarm;

	printf("Estado 'Alarm_Monitor': Desbloqueada.\n");

	do {
		if (AlarmMState.BLOCKED == FALSE) {
			randomTimeout = rand() % 4001 + 1000;	// Gera valor aleatório para timeout de forma à disparar alarmes aleatoriamente entre intervalos de 1 a 5 segundos
			dwRet = WaitForMultipleObjects(2, firstArrayofObjects, FALSE, randomTimeout);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 0) {
				break;
			}
			else if (dwRet == WAIT_TIMEOUT) {
				// Geração de alarme
				Alarm_Creation(Alarm, NSEQ_Alarm, 0);				// Criação da mensagem de alarme
				WaitForSingleObject(hMutexforMailslot, INFINITE);	// Espera o mutex de escrita no mailslot estar liberado
				bStatus = WriteFile(hMailslot, &Alarm, sizeof(Alarm), &dwBytesEnviados, NULL);
				CheckForError(bStatus);
				SetEvent(hMailslotEvent);
				ReleaseMutex(hMutexforMailslot);

				if (NSEQ == 99999) NSEQ_Alarm = 0;
				else
					NSEQ_Alarm++;
			}
			else if (nTipoEvento == 1) {
				printf("Evento de tecla m - Bloqueio thread 'Alarm_Monitor'.\n");
				AlarmMState.BLOCKED = TRUE;
				printf("Estado 'Alarm_Monitor': Bloqueada.\n");
			}
		}
		else {
			dwRet = WaitForMultipleObjects(2, firstArrayofObjects, FALSE, INFINITE);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 0) {
				break;
			}
			else if (nTipoEvento == 1) {
				printf("Evento de tecla m - Desbloqueio thread 'Alarm_Monitor'.\n");
				AlarmMState.BLOCKED = FALSE;
				printf("Estado 'Alarm_Monitor': Desbloqueada.\n");
			}
		}
	} while (1);

	printf("Encerrando thread 'Alarm_Monitor'.\n");

	_endthreadex(0);

	return 0;
}

DWORD WINAPI Messages_Removal() {
	HANDLE firstArrayofObjects[3] = { hEscEvent, hNotificationEvents[3], hSemforList1_OUT };
	HANDLE secondArrayofObjects[3] = { hEscEvent, hNotificationEvents[3] };
	HANDLE thirdArrayofObjects[3] = { hEscEvent, hNotificationEvents[3], hSemforList2_IN };
	HANDLE fourthArrayofObjects[3] = { hEscEvent, hNotificationEvents[3], hSemforList2_OUT };
	HANDLE fifthArrayofObjects[3] = { hEscEvent, hNotificationEvents[3], hMapFileReadEvent };
	DWORD dwRet;
	int nTipoEvento;
	dataMsg Message;
	alarmMsg Alarm;

	printf("Estado 'Messages_Removal': Desbloqueada.\n");

	do {
		if (MsgRmvState.BLOCKED == FALSE) {
			dwRet = WaitForMultipleObjects(3, firstArrayofObjects, FALSE, INFINITE);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 0) {
				break;
			}
			else if (nTipoEvento == 1) {
				printf("Evento de tecla r - Bloqueio thread 'Messages_Removal'.\n");
				MsgRmvState.BLOCKED = TRUE;
				printf("Estado 'Messages_Removal': Bloqueada.\n");
			}
			else if (nTipoEvento == 2) {
				// Lista circular 1 com mensagens à serem retiradas 
				Message = listaCircular1[ocupPositionList1];

				if (Message.DIAG == 55 && Message.ID == 1) {
					// Enviar mensagem de falha no CLP1 para monitoramento de alarme 
					Alarm_Creation(Alarm, Message.NSEQ, 1);				// Criação da mensagem de alarme
					WaitForSingleObject(hMutexforMailslot, INFINITE);	// Espera o mutex de escrita no mailslot estar liberado
					bStatus = WriteFile(hMailslot, &Alarm, sizeof(Alarm), &dwBytesEnviados, NULL);
					CheckForError(bStatus);
					SetEvent(hMailslotEvent);
					ReleaseMutex(hMutexforMailslot);
				}
				else if (Message.DIAG == 55 && Message.ID == 2) {
					// Enviar mensagem de falha no CLP2 para monitoramento de alarme
					Alarm_Creation(Alarm, Message.NSEQ, 2);				// Criação da mensagem de alarme
					WaitForSingleObject(hMutexforMailslot, INFINITE);	// Espera o mutex de escrita no mailslot estar liberado
					bStatus = WriteFile(hMailslot, &Alarm, sizeof(Alarm), &dwBytesEnviados, NULL);
					CheckForError(bStatus);
					SetEvent(hMailslotEvent);
					ReleaseMutex(hMutexforMailslot);
				}
				else {
					// Retirada da lista 1 e depósito na lista 2
					dwRet = WaitForMultipleObjects(3, thirdArrayofObjects, FALSE, 5);
					nTipoEvento = dwRet - WAIT_OBJECT_0;
					if (nTipoEvento == 0) {
						break;
					}
					else if (nTipoEvento == 1) {
						printf("Evento de tecla r - Bloqueio thread 'Messages_Removal'.\n");
						MsgRmvState.BLOCKED = TRUE;
						printf("Estado 'Messages_Removal': Bloqueada.\n");
					}
					else if (nTipoEvento == 2) {
						listaCircular2[freePositionList2] = Message;
						freePositionList2 = (freePositionList2 + 1) % 50;
						bStatus = ReleaseSemaphore(hSemforList2_OUT, 1, NULL);
						CheckForError(bStatus);

						dwRet = WaitForMultipleObjects(3, fourthArrayofObjects, FALSE, INFINITE);
						nTipoEvento = dwRet - WAIT_OBJECT_0;
						if (nTipoEvento == 0) {
							break;
						}
						else if (nTipoEvento == 1) {
							printf("Evento de tecla r - Bloqueio thread 'Messages_Removal'.\n");
							MsgRmvState.BLOCKED = TRUE;
							printf("Estado 'Messages_Removal': Bloqueada.\n");
						}
						else if (nTipoEvento == 2) {
							*lpImage = listaCircular2[ocupPositionList2];
							SetEvent(hMapFileWriteEvent);
							ocupPositionList2 = (ocupPositionList2 + 1) % 50;
							bStatus = ReleaseSemaphore(hSemforList2_IN, 1, NULL);
							CheckForError(bStatus);

							dwRet = WaitForMultipleObjects(3, fifthArrayofObjects, FALSE, INFINITE); // Espera DataViewProcess sinalizar que leu
							nTipoEvento = dwRet - WAIT_OBJECT_0;
							if (nTipoEvento == 0) {
								break;
							}
							else if (nTipoEvento == 1) {
								printf("Evento de tecla r - Bloqueio thread 'Messages_Removal'.\n");
								MsgRmvState.BLOCKED = TRUE;
								printf("Estado 'Messages_Removal': Bloqueada.\n");
							}
							else if (nTipoEvento == 2) {
								// Read do arquivo mapeado feito e sinalizado
							}
						}
					}
				}

				ocupPositionList1 = (ocupPositionList1 + 1) % 100;

				bStatus = ReleaseSemaphore(hSemforList1_IN, 1, NULL);	// Retirada de uma mensagem 
				CheckForError(bStatus);
			}
		}
		else {
			dwRet = WaitForMultipleObjects(2, secondArrayofObjects, FALSE, INFINITE);

			nTipoEvento = dwRet - WAIT_OBJECT_0;
			if (nTipoEvento == 0) {
				break;
			}
			else if (nTipoEvento == 1) {
				printf("Evento de tecla r - Desbloqueio thread 'Messages_Removal'.\n");
				MsgRmvState.BLOCKED = FALSE;
				printf("Estado 'Messages_Removal': Desbloqueada.\n");
			}
		}
	} while (1);

	printf("Encerrando thread 'Messages_Removal'.\n");

	_endthreadex(0);

	return 0;
}

int alarmID_Format() {
	int randomNum;

	randomNum = rand() % 99;
	if (Messages.alarmid == 0 || randomNum != Messages.alarmid) {
		Messages.alarmid = randomNum;
		return randomNum;
	}
	else {
		alarmID_Format();
	}

}

int DIAG_Format() {
	int randomNum;

	randomNum = rand() % 99;	// Aumentado para 99 para a implementação correta, antes estava até 70 pois demorava para gerar algum com DIAG igual a 55
	if (Messages.diag == 0 || randomNum != Messages.diag) {
		Messages.diag = randomNum;
		return randomNum;
		/*if (randomNum != 55) return randomNum;
		else
			return 55;*/
	}
	else {
		DIAG_Format();
	}
}

float P_INT_Format() {
	int randomNum;
	float numDecimal;

	randomNum = rand() % 2001 + 1000;
	if (Messages.p_int == 0 || randomNum != Messages.p_int) {
		Messages.p_int = randomNum;
		if (randomNum % 10 == 0) randomNum++;
		numDecimal = randomNum / 10.0;

		return numDecimal;
	}
	else {
		P_INT_Format();
	}
}

float P_INJ_Format() {
	int randomNum;
	float numDecimal;

	randomNum = rand() % 2001 + 1000;
	if (Messages.p_inj == 0 || randomNum != Messages.p_inj) {
		Messages.p_inj = randomNum;
		if (randomNum % 10 == 0) randomNum++;
		numDecimal = randomNum / 10.0;

		return numDecimal;
	}
	else {
		P_INJ_Format();
	}
}

float TEMP_Format() {
	int randomNum;
	float numDecimal;

	randomNum = rand() % 10001 + 10000;
	if (Messages.temp == 0 || randomNum != Messages.temp) {
		Messages.temp = randomNum;
		if (randomNum % 10 == 0) randomNum++;
		numDecimal = randomNum / 10;

		return numDecimal;
	}
	else {
		TEMP_Format();
	}
}

void Message_Creation(dataMsg& Message, int ID, int NSEQAux) {
	//int _NSEQ = NSEQ_Format(NSEQAux);
	int _DIAG = DIAG_Format();
	float _P_INT = P_INT_Format();
	float _P_INJ = P_INJ_Format();
	float _TEMP = TEMP_Format();
	SYSTEMTIME _TIME;
	GetLocalTime(&_TIME);

	if (_DIAG == 55) {
		Message.NSEQ = NSEQAux;
		Message.ID = ID;
		Message.DIAG = _DIAG;
		Message.P_INT = 0;
		Message.P_INJ = 0;
		Message.TEMP = 0;
		Message.TIME = _TIME;
	}
	else {
		Message.NSEQ = NSEQAux;
		Message.ID = ID;
		Message.DIAG = _DIAG;
		Message.P_INT = _P_INT;
		Message.P_INJ = _P_INJ;
		Message.TEMP = _TEMP;
		Message.TIME = _TIME;
	}
}

void Alarm_Creation(alarmMsg& Alarm, int NSEQAux, int ID) {
	Alarm.NSEQ = NSEQAux;
	GetLocalTime(&Alarm.TIME);

	if (ID == 0) {
		Alarm.ID = alarmID_Format();	// Alarme gera ID aleatório
	}
	else {
		Alarm.ID = ID;	// Alarme gerado por falha de funcionamento em CLP's
	}
}

