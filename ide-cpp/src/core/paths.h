#pragma once
#include <QString>

// lumi 실행 파일 경로 해석: exedir 주변 탐색 후 폴백 (이름은 hostExeName 이 정한다).
QString interpreterPath();

// 셸에 쳐 넣을 lumi 명령. PATH 에 lumi 가 있으면 그대로 "lumi", 없으면 찾아낸 전체 경로.
QString interpreterCommand();
