#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string>
#include <cstring>

#include <queue>
#include <deque>
#include <list>
#include <unordered_map>

#include <thread>
#include <mutex>

#include <random>
#include <ctime>

#include <termios.h>
#include <unistd.h>

using namespace std;

#define FRAME_RATE 30

#define SCREEN_SIZE_X 800
#define SCREEN_SIZE_Y 480
#define GAME_BOARD_SIZE_X 26
#define GAME_BOARD_SIZE_Y 16
#define PIXEL_PER_SLOT 30
#define GLOBE_ASSET_STORE_LOC "./assets/"

#define GAME_START_POS_X 15
#define GAME_START_POS_Y 3
#define GAME_START_DIR MoveDown

struct pos
{
    int x;
    int y;
};
struct zone
{
    pos UL;//upleft
    pos DR;//downright
};


class screenOutput
{
public:
    screenOutput()
    {
        screenfd=open ("/dev/fb0",O_TRUNC | O_RDWR);
        if(screenfd>=0)
        {
            lseek(screenfd, 0, SEEK_SET); 
            screenMem=(int*)mmap(NULL,800*480*4,PROT_READ|PROT_WRITE,MAP_SHARED,screenfd,0);//gwt screen head addr
            if (screenMem==NULL)
                perror("映射错误：");
            else
                for(int i=0; i<800*480; i++)
		            screenMem[i]=0x00;//draw black background
        }
        else
        {
            perror("Error open screen:");
            cout<<"Play it on just terminal :)"<<endl;
        }   
    };
    
    void updateScreen(pos ULpos, string assetName)
    {
        if(ULpos.x<0||ULpos.y<0)
            return;
        string assetLoc= GLOBE_ASSET_STORE_LOC + assetName;
        assestBMP=fopen(assetLoc.c_str(),"r");
        if(assestBMP!=NULL)//make sure screen  and assest file is open
        {
            for(int y=ULpos.y;y<ULpos.y+PIXEL_PER_SLOT;y++)
            {
                fseek(assestBMP,54+((29-(y-ULpos.y))*92),SEEK_SET);//pad to 4 byte 90 ->92
                memset(buffer,0,sizeof(buffer));
                for(int x=0;x<PIXEL_PER_SLOT;x++)
                {
                    fread(&midwayBGR,1,3,assestBMP);
                    screenMem[(y*800+ULpos.x+x)]=0x00<<24|midwayBGR[2]<<16|midwayBGR[1]<<8|midwayBGR[0];
                }
            }    
        }
        fclose(assestBMP);
    }

    ~screenOutput()
    {
        if (screenfd!=-1)
        {
            close(screenfd);
            munmap((void*)screenMem,800*480*4);
        }
        if(assestBMP==NULL)
            fclose(assestBMP);
    };
private:
    int screenfd=-1;
    int* screenMem;
    FILE* assestBMP=NULL;
    unsigned char midwayBGR[3], midwayA=0;//.bmp color
    unsigned char buffer[PIXEL_PER_SLOT*4]={};
};

bool playagainFlag;
class SnakeGame
{
public:
    enum{MoveUp=-2,MoveDown=2,MoveRight=1,MoveLeft=-1}curMoveDir;

    SnakeGame()
    {
        score=0;
        hiScore=0;
        gameBoost=0;
        gameSpeedRate=1;
        curRefreshGuide=&refreshGuide1;
        nextRefreshGuide=&refreshGuide2;
        gameActiveFlag=true;
        srand(time(nullptr));//seed for gen reward

        assetsMap.emplace(snakeGone,"black.bmp");
        assetsMap.emplace(snakeHead,"snakeHead.bmp");
        assetsMap.emplace(snakeBody,"snakeBody.bmp");
        assetsMap.emplace(snakeTail,"snakeTail.bmp");

        assetsMap.emplace(propAir,"black.bmp");
        assetsMap.emplace(propReward,"whiteSmallDot.bmp");
        assetsMap.emplace(propGreatReward,"redDot.bmp");
        assetsMap.emplace(propConcerta,"lighting.bmp");
        assetsMap.emplace(buildWall,"wall.bmp");

        assetsMap.emplace('S',"letter_S.bmp");
        assetsMap.emplace('c',"letter_c.bmp");
        assetsMap.emplace('o',"letter_o.bmp");
        assetsMap.emplace('r',"letter_r.bmp");
        assetsMap.emplace('e',"letter_e.bmp");
        assetsMap.emplace(':',"letter_colon.bmp");

        assetsMap.emplace('0',"num_0.bmp");
        assetsMap.emplace('1',"num_1.bmp");
        assetsMap.emplace('2',"num_2.bmp");
        assetsMap.emplace('5',"num_5.bmp");
        

        placeProp(propReward);//init world
        placeBuild(buildWall,zone{pos{1,2},pos{GAME_BOARD_SIZE_X,2}});
        placeBuild(buildWall,zone{pos{GAME_BOARD_SIZE_X,2},pos{GAME_BOARD_SIZE_X,GAME_BOARD_SIZE_Y}});
        placeBuild(buildWall,zone{pos{1,1},pos{1,GAME_BOARD_SIZE_Y}});
        placeBuild(buildWall,zone{pos{1,GAME_BOARD_SIZE_Y},pos{GAME_BOARD_SIZE_X,GAME_BOARD_SIZE_Y}});

        thread thdSnakeLife(&SnakeGame::snakeLife,this);//snake move
        thread thdRender(&SnakeGame::render,this);//print on screen
        thread thdControl(&SnakeGame::userControl,this);//lisen to input
        thdRender.join();//main thread wait for this to end
        thdSnakeLife.join();
        thdControl.join();
    }

    mutex snakeLock;
    void snakeLife()
    {
        //snake born
        alterSnake(1,pos{GAME_START_POS_X-(abs(GAME_START_DIR)==1?GAME_START_DIR*2:0),GAME_START_POS_Y-(abs(GAME_START_DIR)==2?(GAME_START_DIR/10*2):0)});
        alterSnake(1,pos{GAME_START_POS_X-(abs(GAME_START_DIR)==1?GAME_START_DIR*1:0),GAME_START_POS_Y-(abs(GAME_START_DIR)==2?(GAME_START_DIR/10*1):0)});
        alterSnake(1,pos{GAME_START_POS_X,GAME_START_POS_Y});
        curMoveDir=GAME_START_DIR;

        //snake life
        pos newStep;
        while(gameActiveFlag)
        {
            snakeLock.lock();//lock to access snake deque and curmove
            newStep={snake.front().x+(abs(curMoveDir)==1?curMoveDir:0),snake.front().y+(abs(curMoveDir)==2?((curMoveDir/2)):0)};
            snakeLock.unlock();
            if(hit(newStep))
            {
                gameActiveFlag=false;
            }
            alterSnake(1,newStep);
            
            prop curProp=isGetProp(newStep);
            if(curProp==propReward)//snake reach reward point
            {
                placeProp(propReward);
                if(rand()%100<40)//chance to gen rare prop
                    placeProp(propGreatReward);
                if(rand()%100<15)
                    placeProp(propConcerta);
            }
            else
                alterSnake(-1);
            this_thread::sleep_for(chrono::milliseconds(max((int)(500*gameSpeedRate),50)));//every 0.2- per step
        }
    }

    mutex renderGuideLock;
    void render()
    {
        refreshSlot refreshing;
        //set terminal no echo
        static struct termios old_settings, new_settings;
        tcgetattr(STDIN_FILENO, &old_settings); //get cur setting
        new_settings = old_settings;
        new_settings.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_settings);//set terminal to no echo
        screenOutput classScreenOutput;
        system("clear");
        renderGuideLock.lock();
        nextRefreshGuide->push(refreshSlot{'S',{1,1}});
        nextRefreshGuide->push(refreshSlot{'c',{2,1}});
        nextRefreshGuide->push(refreshSlot{'o',{3,1}});
        nextRefreshGuide->push(refreshSlot{'r',{4,1}});
        nextRefreshGuide->push(refreshSlot{'e',{5,1}});
        nextRefreshGuide->push(refreshSlot{':',{6,1}});
        renderGuideLock.unlock();
        addScore(0);//let score num render
        while(gameActiveFlag)
        {
            renderGuideLock.lock();
            temp=curRefreshGuide;//switch
            curRefreshGuide=nextRefreshGuide;
            nextRefreshGuide=temp;
            unordered_map <char,string>::iterator assetsLoc;//handle return from find() of this map
            while(curRefreshGuide->empty()==0)//load all slot need to be refresh
            {
                //printf("Bp1:%c\n",refreshing.cnt);
                refreshing=curRefreshGuide->front();
                curRefreshGuide->pop();
                cout<<"\033["<<refreshing.p.y<<";"<<refreshing.p.x<<"H"<<(char)refreshing.cnt;//draw on terminal
                assetsLoc=assetsMap.find(refreshing.cnt);//find assetsLocation from this map
                if(assetsLoc!=assetsMap.end())//check if found
                {
                    classScreenOutput.updateScreen(pos{(refreshing.p.x-1)*PIXEL_PER_SLOT,(refreshing.p.y-1)*PIXEL_PER_SLOT},assetsLoc->second);//not found, load placeholder instead
                }
                else
                {
                    classScreenOutput.updateScreen(pos{(refreshing.p.x-1)*PIXEL_PER_SLOT,(refreshing.p.y-1)*PIXEL_PER_SLOT},"placeholder.bmp");//not found, load placeholder instead
                }
            }
            renderGuideLock.unlock();
            fflush(stdout);
            this_thread::sleep_for(chrono::milliseconds((int)(1000/FRAME_RATE)));//30 fps defualt
        }
        system("clear");
        cout<<"Game End. Score: "<<score<<endl<<"Wanna play again? [y/N (defualt)]:"<<endl;//make user type, let userControl stop waiting in getchar and join
        tcsetattr(STDIN_FILENO, TCSANOW, &old_settings);//set terminal to echo
    }

    void userControl()
    {
        char inpt;
        while (true)
        {
            inpt=getchar();
            snakeLock.lock();
            if (inpt=='w' && curMoveDir!=MoveDown)
                curMoveDir=MoveUp;
            else if(inpt=='a'&& curMoveDir!=MoveRight)
                curMoveDir=MoveLeft;
            else if(inpt=='s'&& curMoveDir!=MoveUp)
                curMoveDir=MoveDown;
            else if(inpt=='d'&& curMoveDir!=MoveLeft)
                curMoveDir=MoveRight;
            snakeLock.unlock();

            if(gameActiveFlag==false)//game end, receive respond "wanna play again"
            {  
                if(inpt=='y'|| inpt=='Y')
                {
                    playagainFlag=true;
                    break;
                }
                else
                {
                    break;
                }
            }
            if (inpt=='\033')//esc key
                gameActiveFlag=false;
        }
    }
private:
    int score,hiScore;
    float gameBoost,gameSpeedRate;
    bool gameActiveFlag;

    enum prop {propAir=' ',propReward='*', propGreatReward='#', propConcerta='%'/*利他能 increase speed, increase score*/};    
    enum content {snakeGone=' ', snakeHead='H',snakeBody='B', snakeTail='T'};
    enum mapBuild {buildWall='W', buildPortal='P'};
    deque<pos> snake;

    struct refreshSlot
    {
        char cnt;
        pos p;
    };
    queue<refreshSlot> refreshGuide1,refreshGuide2;
    queue<refreshSlot>* curRefreshGuide,*nextRefreshGuide,*temp;
    unordered_map <char,string> assetsMap;//point to bmp file assests location of prop/build/snake...

    struct propIntel
    {
        prop pr;
        pos po;
    };
    list<propIntel> onlinePropList;

    struct buildingIntel
    {
        mapBuild mb;
        zone z;
    };
    list<buildingIntel> buildingList;

    void alterSnake(int growORcut, pos p={1, 1})
    {
        snakeLock.lock();
        renderGuideLock.lock();
        if(growORcut==1)//move snake head
        {
            snake.emplace_front(p);
            nextRefreshGuide->push(refreshSlot{snakeHead,p});            nextRefreshGuide->push(refreshSlot{snakeBody,pos {(snake.begin()+1)->x,(snake.begin()+1)->y}});//set old head as body
        }
        if(growORcut==-1)//cut snake tail
        {
            p=snake.back();//get pos snake got cut
            nextRefreshGuide->push(refreshSlot{snakeGone,p});
            snake.pop_back();
            p=snake.back();//now last is the tail
            nextRefreshGuide->push(refreshSlot{snakeTail,p});
        }
        snakeLock.unlock();
        renderGuideLock.unlock();
    }

    void addScore(int addScoreVal)
    {
        score+=addScoreVal;
        int tempScore=score,digit;
        renderGuideLock.lock();
        for(int i=5;i>=0;i--)
        {
            digit=tempScore/pow(10,i);
            nextRefreshGuide->push(refreshSlot{(char)('0'+(int)digit),{7+5-i,1}});//update score on screen
            tempScore=tempScore-digit*pow(10,i);
        }
        renderGuideLock.unlock();
    }

    void placeProp(prop prp)
    {
        pos propPos;
        snakeLock.lock();
        do
        {
            propPos={rand()%(GAME_BOARD_SIZE_X-4) +2,rand()%(GAME_BOARD_SIZE_Y-4) +2};//not placing reward on edge
        }
        while(hit(propPos));
        snakeLock.unlock();
        renderGuideLock.lock();
        onlinePropList.insert(onlinePropList.end(),propIntel{prp,propPos});
        nextRefreshGuide->push(refreshSlot{prp,propPos});
        renderGuideLock.unlock();
    }
    float rewardWeight=1;
    prop isGetProp(pos snakePos)//determind if the snake get a prop.
    {
        for(list<propIntel>::iterator p=onlinePropList.begin();p!=onlinePropList.end();p++)//go through the prop list
        {
            if(p->po.x==snakePos.x && p->po.y==snakePos.y)
            {
                if(p->pr==propReward)
                {
                    addScore((int)5*rewardWeight);
                    gameBoost+=0.6;
                    gameSpeedRate=-(gameBoost/(gameBoost+8))+1;
                }
                else if(p->pr==propGreatReward)
                {
                    addScore((int)10*rewardWeight);
                    gameBoost+=1;
                    gameSpeedRate=-(gameBoost/(gameBoost+8))+1;
                }
                else if(p->pr==propConcerta)
                {
                    gameBoost+=6;
                    gameSpeedRate=-(gameBoost/(gameBoost+8))+1;//  formular like: -x/(x-1)+1
                    addScore((int)50*rewardWeight);
                    rewardWeight+=0.2;
                }
                prop temp=p->pr;
                p=onlinePropList.erase(p);//offline the prop from list
                return temp;
            }
        }
        return propAir;
    }

    void placeBuild(mapBuild buildType, zone z)
    {
        buildingIntel newBuild;
        newBuild.mb=buildType;
        newBuild.z=z;
        buildingList.push_back(newBuild);

        renderGuideLock.lock();
        for(int x=z.UL.x;x<=z.DR.x;x++)//render the building
            for(int y=z.UL.y;y<=z.DR.y;y++)
                nextRefreshGuide->push(refreshSlot{buildType,pos{x,y}});
        renderGuideLock.unlock();
    }
    bool hit(pos snakePos)
    {
        for(const pos snakeBodyIdx:snake)//hit snake body
        {
            if(snakePos.x==snakeBodyIdx.x && snakePos.y==snakeBodyIdx.y)
                return true;
        }
        for(const buildingIntel buildingsIdx:buildingList)//hit into building (not all buildings cause hit, like portal)
        {
            if((snakePos.x>=buildingsIdx.z.UL.x&&snakePos.x<=buildingsIdx.z.DR.x) && (snakePos.y>=buildingsIdx.z.UL.y&&snakePos.y<=buildingsIdx.z.DR.y))//run into something?
            {
                if(buildingsIdx.mb==buildWall)
                    return true;
                if(buildingsIdx.mb==buildPortal)
                {/*IDK how to achieve that, I should ask Yeshua or buddha?*/}
            }
        }
        return false;
    }    
};

int main()
{
    do
    {
        playagainFlag=false;
        if(true)
            SnakeGame game;
    }while(playagainFlag);
    
    cout<<"Thank you for playing."<<endl;
    return 0;
};