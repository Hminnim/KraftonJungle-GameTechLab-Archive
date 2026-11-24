프로젝트 선택·생성
에디터 실행 시 Project Browser에서 최근 프로젝트를 열거나 Browse로 .hproject를 선택한다.
현재 W12 콘텐츠는 저장소 루트의 Hitori.hproject를 선택하면 된다.
New Project에서 이름과 위치를 지정하면 Assets/Scenes, Assets/Scripts, GameConfig, Settings, Saved를 만든다.
기본 빈 씬과 Lua 템플릿도 함께 생성한다. 엔진 Resources는 엔진 폴더에서 공유한다.
명령행으로 .hproject 경로 또는 -project="경로"를 주면 브라우저 없이 바로 연다.
게임은 실행 파일 옆 .hproject를 사용하며, 없으면 엔진 폴더의 기본 프로젝트를 찾는다.
Setup/Package.bat Shipping "프로젝트.hproject"로 해당 프로젝트를 Build/Package/Hitori에 패키징한다.

UI Editor
.widget 파일을 열면 파일별 탭이 생기며 선택 상태와 Undo/Redo를 따로 유지한다.
Palette, Hierarchy, Canvas, Details는 이동하거나 별도 창으로 분리할 수 있다.
각 탭의 Window 메뉴에서 패널을 다시 열거나 Reset Layout으로 기본 배치를 복구한다.
닫은 파일 탭의 미저장 내용은 에디터 세션 동안 유지되며, 같은 파일을 다시 열면 이어서 편집한다.
ProgressBar는 Palette에서 추가하고 Details에서 비율, 채움 방향, 색을 설정한다.
Lua에서는 widget:SetPercent(현재값 / 최대값), widget:GetPercent()를 사용한다.

카메라 속도 조절
마우스 오른쪽을 누른채로 휠 Up, Down

선택 액터로 카메라 이동
액터 선택된 채로 F 클릭
Skeletal Viewer에서 본 클릭 후 F 누르면 이동

액터 복사 붙여넣기,
Ctrl C, V, D

액터를 여러개 선택
Shift 클릭으로 여러개 선택

기즈모 평면 이동
기즈모 로컬/월드 변경
뷰포트 왼쪽 상단

현재 레벨 저장
Ctrl S

Spawn 기능
툴바 메뉴로 이전

카메라 옵션(FOV, 위치 등)
뷰포트 오른쪽 상단 탭

아웃라이너에서 액터 이름 바꾸기
F2
레벨을 저장해야 바꾼 이름이 유지됨.

Output Log 창에 로그 복사 가능
Saved/Logs에 로그 파일 생성
엔진 초기화 시 로그도 찍히도록 수정
컴포넌트에 마우스 오른쪽으로 루트 컴포넌트로 설정
