#pragma once

#ifndef GAMETIMER_H
#define GAMETIMER_H

class GameTimer
{
public:
	GameTimer();

	float TotalTime()const; //单位为秒
	float DeltaTime()const; //单位为秒

	void Reset(); //开始消息循环前调用
	void Start(); //解除计时器暂停前调用
	void Stop();  //暂停计时器调用
	void Tick();  //每帧调用.

private:
	double secondsPerCount;
	double deltaTime;

	__int64 baseTime;
	__int64 pausedTime;
	__int64 stopTime;
	__int64 prevTime;
	__int64 currTime;

	bool isStopped;
};

#endif // GAMETIMER_H