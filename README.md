# VEDA_Final_Project
VEDA 최종프로젝트 Github입니다.


### Git 규칙

1. **Branch**
    
    **`main`** (continues) - 프로젝트의 기둥, 성공적으로 배포된 완성 코드를 저장하는 공간
    
    **`hotfix`** - 배포된 기능에 문제가 발생되었을 때 수정을 위한 공간
    
    <aside>
    📝
    
    `hotfix/db-fix`, `hotfix/S12P21A705-13`
    
    </aside>
    
    **`develop`** (continues) - 새로운 기능을 완성하고 테스트 배포를 통해 퀄리티 점검을 진행하는 공간, CI/CD 시스템과 연결되어 테스트 배포가 이뤄짐 (여기에 기본적인 기능을 구현 후에 Merge하면 됩니다.)
    
    **`feature`** - 새로운 기능을 만드는 공간, 각 기능/개발자 별로 나누어 사용하게 됨(브렌치 생성시 기능별로 정리하면 됩니다.)
    
    <aside>
    📝
    
    `feature/connect-mqtt`, `feature/S12P21A705-5`
    
    </aside>
    
2. **Commit**
    
    <aside>
    📝
    
    `[head] commit-message`
    
    </aside>
    
    | **Head Title** | **Description** |
    | --- | --- |
    | `[feat]` | 새로운 기능 |
    | `[fix]` | 버그 수정 |
    | `[test]` | 테스트 |
    | `[docs]` | 문서 추가 및 수정 |
    | `[add]` | 파일 추가  |
    | `[move]` | 파일 이동 |
    | `[remove]` | 파일 삭제 |
    | `[style]` | 코드 스타일 수정 |
    | `[perf]` | 성능 개선 |
3. **Merge**
    
    `feature` → `develop` : 기능 개발이 완료되었을 때 진행합니다. (팀원중 1명이상 승인을 해야 Merge할 수 있습니다. 적극적으로 코드리뷰를 해주세요.)
    
    `hotfix` → `develop` : 긴급한 수정 사항을 반영할 때 진행합니다.
    
    `release` → `main` : 배포가 성공적으로 이뤄졌을 때, 코드 아카이브를 위해 진행합니다. *(팀장이나 PM이 승인해야 함)*
