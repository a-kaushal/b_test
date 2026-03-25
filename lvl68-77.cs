using static PixelMaster.Core.API.PMProfileBuilder;
using PixelMaster.Core.API;
using PixelMaster.Core.Interfaces;
using PixelMaster.Core.Profiles;
using PixelMaster.Core.Behaviors;
using PixelMaster.Core.Behaviors.Transport;
using PixelMaster.Core.Managers;
using PixelMaster.Services.Behaviors;
using System.Collections.Generic;
using System.Numerics;
using System;
using System.Threading;
using System.Threading.Tasks;
using PixelMaster.Server.Shared;
using FluentBehaviourTree;
using PixelMaster.Core.Behaviors.QuestBehaviors;
using PixelMaster.Core.Wow.Objects;
using PixelMaster.Core.Behaviors.Looting;

namespace PixelMaster.ProfileTemplate;
//Do not touch lines above this

public class EmptyProfile : IPMProfile //it is important to implement 'IPMProfile' interface, but u can change 'MyProfile' to any name
{
    List<Mob> avoidMobs = new List<Mob>()
    {
        new Mob{Id=18351, MapId=571, Name="Lump"},
        new Mob{Id=25292, MapId=571, Name="Etaruk"},
        new Mob{Id=25753, MapId=571, Name="Sentry-bot 57-K"},
        new Mob{Id=25814, MapId=571, Name="Fizzcrank Mechagnome"},
    };
    List<Blackspot> blackspots = new List<Blackspot>()
    {
        new Blackspot{Position= new Vector3(-2591.28f, 6829.72f, 33.83f), MapID=571, Radius=10f}
    };
    List<Blackspot> ignoredAreas = new List<Blackspot>()
    {

    };
    List<int> wantedObjects = new List<int>()
    {

    };
    List<MailBox> mailboxes = new List<MailBox>()
    {

    };
    List<Vendor> vendors = new List<Vendor>()
    {
        new Vendor{Id=26596, Name="\"Charlie\" Northtop", MapId=571, Position=new Vector3(4178.47f, 5278.22f, 26.69f), Type=VendorType.Food},
        new Vendor{Id=26599, Name="Willis Wobblewheel", MapId=571, Position=new Vector3(4135.00f, 5281.17f, 25.09f), Type=VendorType.Repair},
    };
    PMProfileSettings CreateSettings()
    {
        return new PMProfileSettings()
        {
            ProfileName = "69-74",
            Author = "",
            Description = "",
            //Objects
            AvoidMobs = avoidMobs,  //sets to the list defined above
            Blackspots = blackspots,//sets to the list defined above
            IgnoredAreas = ignoredAreas,//sets to the list defined above
            Mailboxes = mailboxes,  //sets to the list defined above
            Vendors = vendors,      //sets to the list defined above
            //Player Settings
            MinPlayerLevel = 1,     //Min. player level for this profile. Profile will finish for player bellow this level
            MaxPlayerLevel = 100,   //Max. player level for this profile. Profile will finish for players above this level
            MinDurabilityPercent = 15,  //If any of player items durabilities fell bellow this percent, bot will try will go to vendor to repair/sell/mail/restock items
            MinFreeBagSlots = 1,        //If player free general bag slots reach this number, bot will go to vendor to sell/mail/restock items
            
            //Restocking
            Foods = (20, new int[] { 26596 }),
            Drinks = (20, new int[] { 26596 }),
            Arrows = (1000, new int[] {  }),
            Bullets = (1000, new int[] {  }),

            //Keep items are items you want to skip from selling or mailing
            KeepItems = new List<int> {
                3182    // Spider Silk
            },

            OnTaskFailure = TaskFailureBehavior.ReturnFailure,
        };
    }
    public IPMProfileContext Create()
    {
        var ME = ObjectManager.Instance.Player;//just a shortcut to use inside profile
        var settings = CreateSettings(); //Creates profile settings from the above method
        StartProfile(settings); //Starting the profile using the settings
        //-------------------------------START PROFILE-------------------------------

        IF(() => ObjectManager.Instance.CurrentMap.MapID != 571, "Go to Northrend", onChildFailure: TaskFailureBehavior.ReturnSuccess);
            MoveTo(0, "(-8302.65, 1392.79, 4.66)", TaskName: "Go to Stormwind Docks", CanUseTaxi: true);
            LogInfo("INTERACT WITH NPC");
            InteractWithNpc(MapId: 0, QuestId: 0, MobId: 26548, NumTimes: 1, GossipOptions: "SelectGossipOption(1)");
            While(() => true, onChildFailure: TaskFailureBehavior.Continue);//starts while
                LogInfo("RESTART AUTO");
            EndWhile();
        EndIF();
        GrindMobsUntil(MapId: 571, PlayerLevelReached: 77, MobIDs: "25817", TaskName: "Grind to level 77", Hotspots: "(3813.40, 5121.93, -1.51),(3819.01, 5102.54, -1.51),(3820.37, 5082.34, -1.51),(3815.21, 5062.88, -1.51),(3810.19, 5043.45, -1.51),(3807.73, 5023.48, -1.51),(3815.74, 5004.94, -1.51),(3835.06, 4999.00, -1.42),(3846.70, 5015.51, -1.41),(3854.32, 5034.24, -1.49),(3861.34, 5053.23, -1.51),(3869.41, 5071.84, -1.51),(3872.79, 5091.72, -1.51),(3892.58, 5087.88, -0.98),(3912.81, 5087.00, -1.51),(3928.93, 5098.85, -1.51),(3945.20, 5110.95, -1.51),(3964.06, 5118.32, -1.51),(3982.22, 5126.85, -1.51),(3988.78, 5145.81, -1.50),(3969.91, 5152.54, -1.51),(3950.49, 5146.68, -1.51),(3931.86, 5139.00, -1.51),(3912.20, 5135.30, -1.51),(3912.39, 5155.45, -0.19),(3914.61, 5171.61, 11.85),(3918.27, 5191.31, 13.75),(3902.10, 5203.56, 13.04),(3883.34, 5210.77, 13.04),(3865.46, 5201.76, 13.04),(3846.69, 5194.81, 13.04),(3833.04, 5179.96, 13.05)", KillAllMobs: true, LootMobs: true, PullRange: 105, HotSpotRange: 85, CanFly: false);  //Hellfire Peninsula

        LogInfo("Write Lvl70-74(Cat)");
        //-------------------------------END PROFILE-------------------------------
        return EndProfile();
    }
}
