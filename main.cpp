#include <iostream>
#include <exception>

#include <queue>
#include <deque>
#include <list>

#include <thread>
#include <mutex>

#include <random>
#include <ctime>

#include <termios.h>
#include <unistd.h>

using namespace std;

#define FRAME_RATE 30

#define GAME_BOARD_SIZE_X 30
#define GAME_BOARD_SIZE_Y 12

#define GAME_START_POS_X 15
#define GAME_START_POS_Y 6
#define GAME_START_DIR MoveDown

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
        alterSnake(1,pos{20,10});
        placeProp(propReward);
        curMoveDir=GAME_START_DIR;

        //snake life
        pos newStep;
        while(gameActiveFlag)
        {
            newStep={snake.front().x+(abs(curMoveDir)==1?curMoveDir:0),snake.front().y+(abs(curMoveDir)==2?((curMoveDir/2)):0)};
            if(hitSnake(newStep))
                throw logic_error("It hurt!");//snake eat itself
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
            this_thread::sleep_for(chrono::milliseconds(max((int)(200*gameSpeedRate),50)));//every 0.5+ per step
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
        system("clear");
        renderGuideLock.lock();
        nextRefreshGuide->push(refreshSlot{{1,0},'S'});
        nextRefreshGuide->push(refreshSlot{{2,0},'c'});
        nextRefreshGuide->push(refreshSlot{{3,0},'o'});
        nextRefreshGuide->push(refreshSlot{{4,0},'r'});
        nextRefreshGuide->push(refreshSlot{{5,0},'e'});
        nextRefreshGuide->push(refreshSlot{{6,0},':'});
        renderGuideLock.unlock();
        addScore(0);//let score num render
        while(gameActiveFlag)
        {
            renderGuideLock.lock();
            temp=curRefreshGuide;//switch
            curRefreshGuide=nextRefreshGuide;
            nextRefreshGuide=temp;
            while(curRefreshGuide->empty()==0)//load all slot need to be refresh
            {
                //printf("Bp1:%c\n",refreshing.cnt);
                refreshing=curRefreshGuide->front();
                curRefreshGuide->pop();
                //printf("[\03322;22H%c",refreshing.cnt);//draw
                cout<<"\033["<<refreshing.p.y<<";"<<refreshing.p.x<<"H"<<(char)refreshing.cnt;
            }
            renderGuideLock.unlock();
            fflush(stdout);
            this_thread::sleep_for(chrono::milliseconds((int)(1000/FRAME_RATE)));//30 fps defualt
        }
        system("clear");
        tcsetattr(STDIN_FILENO, TCSANOW, &old_settings);//set terminal to echo
    }

    void userControl()
    {
        char inpt;
        while (gameActiveFlag)
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
            if (inpt=='\033')//esc key
                gameActiveFlag=false;
        }

    }

private:
    int score,hiScore;
    float gameBoost,gameSpeedRate;
    bool gameActiveFlag;

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
    deque<pos> snake;
    enum prop {propAir='\0',propReward='*', propGreatReward='#', propConcerta='%'/*利他能 increase speed, increase score*/};    

    enum content {snakeGone=' ', snakeHead='H',snakeBody='B', snakeTail='T', snakeFood='*'};
    struct refreshSlot
    {
        pos p;
        char cnt;
    };
    queue<refreshSlot> refreshGuide1,refreshGuide2;
    queue<refreshSlot>* curRefreshGuide,*nextRefreshGuide,*temp;
    void alterSnake(int growORcut, pos p={1, 1})
    {
        //printf("{%d:%d,%d}",growORcut,p.x,p.y);
        snakeLock.lock();
        renderGuideLock.lock();
        if(growORcut==1)//move snake head
        {
            snake.emplace_front(p);
            nextRefreshGuide->push(refreshSlot{p,snakeHead});
            nextRefreshGuide->push(refreshSlot{pos {(snake.begin()+1)->x,(snake.begin()+1)->y},snakeBody});//set old head as body
        }
        if(growORcut==-1)//cut snake tail
        {
            p=snake.back();//get pos snake got cut
            nextRefreshGuide->push(refreshSlot{p,snakeGone});
            snake.pop_back();
            p=snake.back();//now last is the tail
            nextRefreshGuide->push(refreshSlot{p,snakeTail});
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
            nextRefreshGuide->push(refreshSlot{{7+5-i,0},(char)('0'+(int)digit)});//update score on screen
            tempScore=tempScore-digit*pow(10,i);
        }
        renderGuideLock.unlock();
    }

    struct propIntel
    {
        pos po;
        prop pr;
    };
    list<propIntel> onlinePropList;
    void placeProp(prop prp)
    {
        pos propPos;
        snakeLock.lock();
        do
        {
            propPos={rand()%(GAME_BOARD_SIZE_X-4) +2,rand()%(GAME_BOARD_SIZE_Y-4) +2};//not placing reward on edge
        }
            
        while(hitSnake(propPos));
        snakeLock.unlock();
        renderGuideLock.lock();
        onlinePropList.insert(onlinePropList.end(),propIntel{propPos,prp});
        nextRefreshGuide->push(refreshSlot{propPos,prp});
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

    bool hitSnake(pos snakePos)
    {
        for(const pos snakeBodyIdx:snake)
        {
            if(snakeBodyIdx.x==snakePos.x && snakeBodyIdx.y==snakePos.y)
                return true;
        }
        return false;
    }
};

int main()
{
    SnakeGame game;
    cout<<"Done."<<endl;
}