# Hitori 공유 저장소와 덤프 사용 안내

| 할 일 | 사용할 화면 |
| --- | --- |
| 팀 공유 저장소 만들기 | 서버 PC의 파일 탐색기 |
| 내 PC에 서버 주소 지정하기 | Hitori 배포 및 덤프 관리 → **저장소 설정** |
| 빌드를 Release로 게시하기 | Visual Studio에서 빌드 → **Release 게시** |
| 크래시 덤프 제출 또는 새 Crash 배포 | Hitori 크래시 리포트 → **덤프 제출** |
| 팀원이 올린 덤프 분석하기 | 버전 매니저 → **선택 항목 열기** → Visual Studio |

## 1. 서버 PC에서 공유 폴더 만들기

팀원이 접근할 수 있는 내부망의 Windows PC 한 대에서 진행한다. 이 안내에서는 공유 이름을 `VersionStore`로 사용한다.

1. 파일 탐색기에서 저장할 드라이브에 `VersionStore` 폴더를 만든다. 그 안에 `symbols`와 `VersionData` 폴더를 각각 만든다.
2. `VersionStore` 폴더를 우클릭하고 **더 많은 옵션 표시 → 액세스 권한 부여(Give access to) → 특정 사용자(Specific people)**를 연다. 메뉴가 보이지 않으면 **속성 → 공유 → 공유**를 연다.
3. 서버 PC에서 인증할 수 있는 팀원의 Windows 계정이나 팀 그룹을 추가한다. **덤프와 심볼을 조회할 계정은 읽기**, **덤프 제출이나 배포 게시·삭제를 할 계정은 읽기/쓰기**를 부여하고 **공유**를 누른다.
4. 공유 완료 화면에 표시된 네트워크 경로를 확인한다. 예를 들어 호스트 PC의 이름이 `DESKTOP-6KQUH32`라면 주소는 `\\DESKTOP-6KQUH32\VersionStore`다. 내부망의 다른 PC로 옮길 때는 그 PC의 이름으로 바꾼 주소를 팀원에게 전달한다.
5. 다른 팀원 PC의 **파일 탐색기 주소 표시줄**에 그 주소를 붙여 넣는다. Windows 자격 증명을 요청하면 공유 권한이 부여된 계정으로 로그인한다. `symbols`와 `VersionData`가 보이면 연결된 것이다.

공유 화면 목록에 쓸 계정이 없으면 서버 PC에서 **설정 → 계정 → 다른 사용자 → 계정 추가**를 연다. **이 사람의 로그인 정보를 가지고 있지 않습니다 → Microsoft 계정 없이 사용자 추가**를 선택해 암호가 있는 표준 로컬 계정을 만든다. 공유 화면으로 돌아와 그 계정을 **추가**하고 권한을 선택한 뒤 **공유**를 누른다. 다른 PC의 자격 증명 창에는 `서버PC이름\계정이름`과 그 계정의 암호를 입력한다. [Microsoft의 Windows 사용자 추가 안내](https://support.microsoft.com/en-us/windows/security/identity-signin/manage-user-accounts-in-windows)

첫 게시가 성공하면 프로그램이 두 폴더 안에 필요한 파일과 하위 폴더를 만든다. 처음 두 폴더가 비어 있어도 저장소를 지정할 수 있다.

공유가 열리지 않으면 호스트가 켜져 있고 내부망으로 접근 가능한지, **설정 → 네트워크 및 인터넷 → 고급 네트워크 설정 → 고급 공유 설정 → 개인 네트워크**에서 **파일 및 프린터 공유**가 켜져 있는지 확인한다. 탐색기의 **네트워크** 목록에서 호스트를 찾고 싶다면 **네트워크 검색**도 켠다. 공유는 열리는데 파일을 쓰거나 지울 수 없으면 호스트 폴더의 **속성 → 공유**와 **속성 → 보안** 양쪽에서 해당 계정의 권한을 확인한다. [Microsoft Windows 파일 공유 안내](https://support.microsoft.com/en-us/windows/experience/connectivity-networking/file-sharing-over-a-network-in-windows)

## 2. 개발자 PC에서 도구 준비하고 서버 지정하기

<a id="git-install"></a>

빌드 전에 Git for Windows 설치 프로그램을 실행하고 아래 두 항목을 함께 선택한다. [Git for Windows 다운로드](https://gitforwindows.org/)

- **Adjusting your PATH environment**에서 **Git from the command line and also from 3rd-party software**를 선택한다. 빌드 스냅샷 도구가 `git.exe`를 찾는 데 필요한 설정이다.
- **Choose a credential helper**에서 **Git Credential Manager**를 선택한다. 배포와 이슈 작성 시 GitHub 로그인 정보를 사용하는 데 필요한 설정이다. [GitHub의 Git Credential Manager 안내](https://docs.github.com/en/get-started/git-basics/caching-your-github-credentials-in-git#git-credential-manager)

설치 후 Visual Studio를 완전히 종료하고 다시 연다.

1. 파일 탐색기에서 `Setup/SlnGenerator.bat`을 더블클릭한다. 프로젝트 루트에 생성된 `Hitori.slnx`를 Visual Studio에서 연다.
2. Visual Studio에서 사용할 구성을 선택한다. 이 안내의 예시는 **Release / x64**다. **솔루션 탐색기 → HitoriEditor 우클릭 → 빌드**를 선택하면 같은 구성의 버전 매니저와 크래시 리포터도 빌드된다.
3. 탐색기에서 `Build/Bin/Release-windows-x86_64/HitoriVersionManager.exe`를 연다. Debug로 빌드했다면 `Build/Bin/Debug-windows-x86_64`에 있다.
4. 화면 상단 **저장소 설정 → 주소**에 `\\DESKTOP-6KQUH32\VersionStore`처럼 서버의 공유 루트 주소를 넣고 **연결**을 누른다. 연결 후 목록에 **저장소에 연결됨**이 표시된다. 처음 세팅한 서버라면 게시 목록은 비어 있다. 이 설정은 같은 Windows 사용자로 실행하는 크래시 리포터에도 적용된다.

### 배포를 게시하거나 삭제할 PC의 추가 설정

- **Windows SDK에서 Debugging Tools for Windows**를 선택해 설치한다. 기본 설치 위치를 사용했다면 파일 탐색기에서 `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64`를 열어 `symstore.exe`를 확인한다. `pdbstr.exe`는 그 안의 `srcsrv` 폴더에 있다. 게시 도구는 `symstore.exe`로 배포 파일을 등록·삭제하고, 당시 소스가 없는 Crash 배포의 PDB를 처리할 때 `pdbstr.exe`를 사용한다. **Visual Studio로 덤프를 분석하거나 기존 배포에 덤프만 붙일 때는 이 별도 설치가 필요하지 않다.** [Microsoft 설치 안내](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/debugger-download-tools)
- Windows Software Development Kit 이 설치되어 있는경우 (제어판 프로그램에서 확인 가능) 아무 버전이나 우클릭하여 Change 선택 - Change - Next - 다른것 건드리지 말고 Debugging Tools for Windows 체크 - Change 를 누르는 것으로 위에서 설명한 도구들을 설치할 수 있다.
- 위의 Git 설치 단계에서 **Git Credential Manager**까지 선택했는지 확인한다. 설치 설정을 확인하려면 해당 안내로 이동한다. [Git 설치 단계로 이동](#git-install)
- Visual Studio의 **Git → 설정**에서 **사용자 이름**과 **이메일**을 확인하고, **Git 저장소 설정 → 원격**에서 `origin`이 원본 팀 GitHub 저장소를 가리키는지 확인한다. HTTPS 저장소에 접근할 때 Git이 브라우저 로그인을 요청하면 팀 저장소에 쓰기 권한이 있는 계정으로 로그인한다. 이때 저장한 Git 자격 증명을 버전 매니저도 사용한다. [Visual Studio의 Git 설정](https://learn.microsoft.com/en-us/visualstudio/version-control/git-settings)

팀의 기본 주소를 한 번 정해 두려면 Visual Studio나 메모장에서 `GameConfig/VersionStore.json`의 `storageRoot`를 편집한다. 주소가 `\\DESKTOP-6KQUH32\VersionStore`라면 JSON 값에는 `"\\\\DESKTOP-6KQUH32\\VersionStore"`라고 적는다. **저장소 설정**에서 개인 주소를 이미 저장한 PC에서는 그 주소가 팀 기본값보다 우선한다.

## 3. Release 게시하기

1. Visual Studio에서 게시할 Git 브랜치와 빌드 구성을 확인한다. **HitoriEditor** 또는 **HitoriGame** 프로젝트를 우클릭해 **빌드**한다.
2. 버전 매니저에서 **Release 게시**를 누른다. 창에 표시된 **빌드**의 대상·구성과 **빌드에 기록된 커밋**을 확인한다. 이 창은 해당 Git 작업 폴더에서 마지막으로 성공한 버전 빌드를 보여준다. 다른 대상을 마지막으로 빌드해서 원하는 대상이 표시되지 않으면, Visual Studio에서 원하는 프로젝트를 **다시 빌드**한 뒤 창을 다시 연다.
3. 올릴 GitHub 브랜치를 선택한다.
   - **새 브랜치**를 선택하면 입력한 이름으로 새 원격 브랜치를 만들고, 빌드에 기록된 커밋을 올린다.
   - **현재 브랜치**를 선택하면 같은 이름의 원격 브랜치에 빌드에 기록된 커밋을 올린다. 빌드 당시 브랜치와 현재 브랜치가 일치할 때 선택할 수 있다.
4. **게시**를 누른다. 목록에 해당 **Release 배포**와 커밋 SHA가 나타나는지 확인한다. 이미 같은 빌드의 활성 배포가 있으면 해당 배포를 사용한다.

게시 도구가 일치하는 EXE·DLL·PDB를 등록한다. **게시 기록 검색**의 **커밋 SHA**와 **브랜치 / 배포 ID**는 이미 올라간 항목을 찾을 때 쓰는 칸이다. 새 배포에 쓸 SHA는 빌드 기록에서 자동으로 읽는다.

## 4. 덤프 제출과 Crash 배포

### 크래시 직후 제출하기

1. 디버거 없이 실행하던 에디터나 게임에서 크래시가 발생하면, 로컬 덤프가 저장된 뒤 **Hitori 크래시 리포트** 창이 열린다.
2. 화면의 **공유 저장소**와 **로컬 덤프**를 확인한다. 이미 배포된 빌드에서 난 크래시라면 **덤프 제출**을 누른다.
3. 아직 배포되지 않은 개발 빌드라면 **개발자 권한 확인**을 누른다. 이어서 **기존 배포가 없으면 새 Crash 배포 만들기**를 체크한다. 이슈도 작성할 때는 **GitHub 이슈 작성**을 체크하고 제목과 내용을 입력한다.
4. **덤프 제출**을 누른다. `덤프 … 제출 완료`가 표시되면 버전 매니저에서 **목록 조회**를 눌러 덤프 번호를 확인한다.

### Visual Studio에서 저장한 덤프나 보관해 둔 덤프 제출하기

1. Visual Studio가 예외에서 멈춘 경우 **Debug → Save Dump As…**를 눌러 `.dmp`를 저장한다. [Microsoft의 Visual Studio 덤프 저장 안내](https://learn.microsoft.com/en-us/visualstudio/debugger/using-dump-files#create-a-dump-file)
2. 버전 매니저의 **로컬 덤프 제출**에서 저장한 `.dmp` 파일을 그대로 선택한다. 크래시 리포트 창이 열리면 위와 같은 방식으로 제출한다. 폴더를 고르거나 파일명을 바꿀 필요는 없다.

기존 배포가 있으면 덤프를 그 아래에 붙인다. 새 Crash 배포에는 덤프 당시의 EXE와 필요한 DLL이 있어야 한다. 당시 소스를 확인할 수 있으면 `crash/<번호>` 브랜치 아래에 등록되고, 바이너리만 확보됐다면 목록의 커밋 열에 **확인된 소스 없음**이 표시된다. 같은 `.dmp`를 다시 제출하면 새 덤프 번호가 발급된다.

게시 도중 실패한 조사를 정리하려면 버전 매니저에서 해당 **Crash 브랜치**를 선택한다. 덤프가 남아 있으면 **덤프 일괄 삭제**로 지운 뒤 **조사 종료**를 누른다. 정리 확인 창에는 중단된 배포 작업 개수도 표시된다. 확인 후 해당 작업도 닫힌다. 심볼 등록까지 진행됐던 작업은 심볼을 정리하는 PC에 Debugging Tools for Windows가 필요하다.

## 5. 다른 팀원이 올린 덤프를 Visual Studio에서 열기

1. 버전 매니저에서 **목록 조회**를 누른다. 항목이 많으면 **브랜치 / 배포 ID** 또는 **커밋 SHA**로 검색한다. 배포 아래의 **덤프 #번호**를 선택하고 **선택 항목 열기**를 누른다. Visual Studio에서 직접 열려면 **File → Open → File**에서 `crash.dmp`를 선택한다.
2. Visual Studio의 덤프 요약에서 **Set symbol paths**를 열거나, **Tools → Options**에서 `Symbols`를 검색한다. **Search Locations → + Add**에서 팀 심볼 서버 위치 (예 : `\\DESKTOP-6KQUH32\VersionStore\symbols`)를 추가하고 활성화한다. 버전 매니저에는 공유 루트를, Visual Studio에는 그 아래 `symbols` 경로를 입력한다. Windows DLL의 PDB도 받을 수 있도록 **Microsoft 기호 서버에서 기호 다운로드(Microsoft Symbol Servers)**를 체크한다. [Visual Studio 심볼 설정](https://learn.microsoft.com/en-us/visualstudio/debugger/specify-symbol-dot-pdb-and-source-files-in-the-visual-studio-debugger#configure-location-of-symbol-files-and-loading-options)
3. 덤프 요약에서 **Debug with Native Only**를 누른다. **디버그 → 창 → 모듈(Debug → Windows → Modules)** 또는 **Ctrl + Alt + U**로 모듈 창을 열어 `HitoriEditor.exe` 또는 `HitoriGame.exe`의 **기호 상태(Symbol Status)**를 확인한다. 자동으로 로드되지 않으면 해당 모듈을 우클릭하고 **기호 로드(Load Symbols)**를 누른다. 그래도 실패하면 **기호 로드 정보(Symbol Load Information)**에서 실제로 검색한 위치를 확인한다. 상태 문구에 따른 조치는 [덤프의 심볼 로드 트러블슈팅](#dump-symbol-troubleshooting)을 따른다. [Visual Studio의 Modules 창](https://learn.microsoft.com/en-us/visualstudio/debugger/how-to-use-the-modules-window)
4. **Debug → Windows → Call Stack**에서 Hitori 함수가 표시된 프레임을 더블클릭한다. 배포에 소스가 연결돼 있다면 Visual Studio 옵션에서 **Enable Source Link support**를 확인하고, 해당 GitHub 저장소에 접근할 수 있는 계정을 사용한다. **확인된 소스 없음** 상태여도 PDB가 로드되면 함수명과 콜스택을 조사할 수 있다. [Visual Studio의 네이티브 Source Link 지원](https://learn.microsoft.com/en-us/cpp/build/reference/sourcelink)

## 6. 덤프 분석 트러블슈팅

<a id="dump-symbol-troubleshooting"></a>

### 함수명이나 소스가 보이지 않거나 PDB가 로드되지 않을 때

먼저 **디버그 → 창 → 모듈** 또는 **Ctrl + Alt + U**로 모듈 창을 연다. `ntdll.dll`, `KERNELBASE.dll`, `HitoriEditor.exe` 같은 EXE·DLL 목록이 표시되는 창이다. 찾을 모듈명을 검색하고 **기호 상태**를 확인한다. 에디터 덤프는 `HitoriEditor.exe`, 게임 덤프는 `HitoriGame.exe`부터 확인한다.

| 화면의 상태 | 확인할 내용과 조치 |
| --- | --- |
| **포함/제외 설정으로 기호 로드가 비활성화되었습니다**, **포함된 모듈 목록에 지정되어 있지 않아서 기호 로드를 건너뛰었습니다**, **PDB 포함 안 됨** | 자동 로드 대상에서 빠진 상태다. 해당 모듈을 우클릭해 **기호 로드(Load Symbols)**를 선택한다. 이 문구만으로 서버에 PDB가 없다고 판단하지 않는다. |
| **PDB 파일을 찾거나 열 수 없습니다(Cannot find or open the PDB file)** | 해당 모듈을 우클릭해 **기호 로드 정보(Symbol Load Information)**를 연다. 아래의 서버 설정과 실제 검색 경로, 접근 오류를 확인한다. |
| PDB가 모듈과 일치하지 않는다는 메시지 | 덤프 당시 EXE·DLL과 짝인 PDB가 필요하다. 파일명이 같아도 다른 빌드의 PDB는 사용할 수 없다. 당시 배포의 파일을 확보한다. |
| 기호는 로드됐지만 **소스를 찾을 수 없음** | 소스 조회 문제를 확인한다. Hitori 모듈은 **Source Link 지원 사용**과 해당 GitHub 커밋·파일의 접근 가능 여부를 확인한다. 배포에 **확인된 소스 없음**이 표시되면 함수명·콜스택·디스어셈블리로 조사할 수 있다. Windows DLL의 공개 PDB를 받았다고 그 DLL의 소스까지 제공되는 것은 아니다. |

서버 주소는 **PDB를 어디서 검색할지**를 지정하고, 포함·제외 설정은 **어떤 모듈의 PDB를 자동 검색할지**를 정한다. 서버를 체크했어도 자동 검색 대상에 포함되지 않은 모듈은 건너뛸 수 있다. 계속 사용할 모듈은 모듈 창에서 우클릭해 **항상 자동으로 로드(Always Load Automatically)**를 선택하고, 다음 디버깅 세션에서 확인한다. **설정 → 디버깅 → 기호 → 검색 및 로드**의 모듈 포함·제외 목록에서도 조정할 수 있다. 필요한 모듈부터 지정하면 모든 모듈의 PDB를 검색하는 데 드는 시간을 줄일 수 있다. [Microsoft의 모듈별 기호 로드 안내](https://learn.microsoft.com/en-us/visualstudio/debugger/how-to-use-the-modules-window#load-symbols)

### Windows DLL 또는 Hitori의 PDB를 찾지 못할 때

1. **도구 → 옵션**에서 `기호` 또는 `Symbols`를 검색해 **디버깅 → 기호 → 검색 위치(Search Locations)**를 연다. Visual Studio 버전에 따라 메뉴 이름과 배치가 다를 수 있다.
2. `KERNELBASE.dll`, `kernel32.dll`, `ntdll.dll` 등 Windows 모듈은 **Microsoft 기호 서버에서 기호 다운로드(Microsoft Symbol Servers)**를 체크한다. 인터넷 연결이 필요하며, 해당 Windows 모듈과 일치하는 공개 PDB를 받아 사용한다.
3. `HitoriEditor.exe`, `HitoriGame.exe` 등 팀 모듈은 **기호 파일(.pdb) 위치**에서 팀 서버의 `\\서버PC이름\VersionStore\symbols` 경로를 추가하고 **사용함**을 체크한다. Windows 파일 탐색기에서도 같은 경로를 열어 공유 폴더에 접근할 수 있는지 확인한다. 자격 증명을 요청하면 서버의 공유 권한을 받은 계정으로 로그인한다. Microsoft 기호 서버는 팀에서 만든 Hitori PDB를 제공하지 않는다.
4. 모듈 창으로 돌아와 해당 모듈을 우클릭해 **기호 로드**를 누른다. 계속 실패하면 **기호 로드 정보**에 나오는 검색 경로와 오류를 확인한다. Hitori PDB가 당시 배포에 포함되지 않았다면, 서버 주소나 로드 설정을 바꿔도 해당 PDB를 가져올 수 없다.

이 설정은 Visual Studio가 PDB를 받기 위한 설정이다. 배포 파일을 등록·삭제할 때 사용하는 `symstore.exe`를 분석 PC에 별도로 설치할 필요는 없다. [Microsoft의 심볼 검색 위치와 서버 설정 안내](https://learn.microsoft.com/en-us/visualstudio/debugger/specify-symbol-dot-pdb-and-source-files-in-the-visual-studio-debugger#configure-location-of-symbol-files-and-loading-options)

### `KERNELBASE.dll`에서 처리되지 않은 예외가 표시될 때

`CauseCrash`로 만든 덤프에서 예외 코드가 **`0xEFFFFFFF`**라면 테스트 명령이 발생시킨 예외다. 덤프 분석 화면은 저장된 예외 당시 상태를 보여준다. 현재 구현은 `RaiseException()`을 호출하므로 Windows 함수가 실행되던 `KERNELBASE.dll` 프레임이 먼저 보일 수 있다.

예외 알림을 닫고 필요한 모듈의 기호를 로드한 뒤, **디버그 → 창 → 호출 스택(Debug → Windows → Call Stack)**을 연다. `HitoriEditor.exe!…` 또는 `HitoriGame.exe!…` 프레임을 더블클릭해 우리 코드의 호출 위치로 이동한다. `kernelbase.pdb`를 로드하는 것과 Hitori 모듈의 PDB를 로드하는 것은 각각 확인해야 한다.

### `Unable to load resource dll`로 디버깅이 시작되지 않을 때

**Debug with Native Only**를 눌렀을 때 **출력 → 디버그**에 `FATAL ERROR: Unable to load resource dll.`이 나오면, 실행 중인 Visual Studio의 설치 경로에서 `Common7\IDE\vsdbg\1033`에 `vsdbg.resources.dll`과 `vsdebugeng.impl.resources.dll`이 있는지 확인한다. `1033`은 영어 디버거 리소스 폴더다. 한국어 Visual Studio 설치에도 이 파일들이 포함될 수 있다. [Visual Studio 언어 ID](https://learn.microsoft.com/en-us/visualstudio/ide/reference/lcid-devenv-exe)

1. **Visual Studio Installer → 해당 설치 → 더 보기 → 복구**를 실행한다. 복구 후 같은 `crash.dmp`를 열고 **Debug with Native Only**로 재시도한다. [Visual Studio 복구 안내](https://learn.microsoft.com/en-us/visualstudio/install/repair-visual-studio)
2. 복구 후에도 같은 오류가 나면 **Visual Studio Installer → 해당 설치 → 수정 → 언어 팩**에서 **English**를 선택해 설치한다. Visual Studio의 표시 언어는 한국어로 유지한 채 다시 시작하고 같은 덤프를 연다. [Visual Studio 언어 팩 설치 안내](https://learn.microsoft.com/en-us/visualstudio/install/modify-visual-studio#modify-language-packs)
