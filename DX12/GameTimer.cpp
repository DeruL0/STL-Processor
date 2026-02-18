#include <windows.h>
#include "GameTimer.h"

GameTimer::GameTimer()
:secondsPerCount(0.0), deltaTime(-1.0), baseTime(0), pausedTime(0), prevTime(0), currTime(0), isStopped(false){
	__int64 countsPerSec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	secondsPerCount = 1.0 / (double)countsPerSec;
}

float GameTimer::TotalTime()const{
	//如果正在停止，则不用管停止-现在时间
	//如果之前停止过，那么之前的停止片段也不用管
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------------*------> time
	//  mBaseTime       mStopTime        startTime     mStopTime    mCurrTime
	if (isStopped){
		return (float)(((stopTime - pausedTime) - baseTime) * secondsPerCount);
	}

	//如果之前停止过，那么之前的停止片段不用管
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------> time
	//  mBaseTime       mStopTime        startTime     mCurrTime
	else{
		return (float)(((currTime - pausedTime) - baseTime) * secondsPerCount);
	}
}


void GameTimer::Start(){
	__int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);

	// 累加调用stop和start这对方法间的暂停时间间隔
	//                     |<-------d------->|
	// ----*---------------*-----------------*------------> time
	//  mBaseTime       mStopTime        startTime     
	
	//如果从停止时刻继续计时
	if (isStopped){
		//累加暂停时间
		pausedTime += (startTime - stopTime);
		//将preTime设置为当前时间
		prevTime = startTime;
		//重置isStopped
		stopTime = 0;
		isStopped = false;
	}
}

void GameTimer::Stop(){
	//如果已经暂停什么都不做
	if (!isStopped){
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

		//否则保存停止时刻后设置isStopped
		stopTime = currTime;
		isStopped = true;
	}
}


float GameTimer::DeltaTime()const{
	return (float)deltaTime;
}

void GameTimer::Reset(){
	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

	baseTime = currTime;
	prevTime = currTime;
	stopTime = 0;
	isStopped = false;
}


void GameTimer::Tick(){
	if (isStopped){
		deltaTime = 0.0;
		return;
	}
	
	//获取本帧开始显示时刻
	__int64 currTime_;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime_);
	currTime = currTime_;

	//与前一帧时间差
	deltaTime = (currTime - prevTime) * secondsPerCount;

	//准备计算本帧与下一帧的时间差
	prevTime = currTime;

	//使时间差为非负(处理器节能模式or计算时间差切换处理器)
	if (deltaTime < 0.0){
		deltaTime = 0.0;
	}
}
