import { Routes } from '@angular/router';
import { SetupComponent } from './pages/setup/setup.component';
import { LoginComponent } from './pages/login/login.component';
import { BoardComponent } from './pages/board/board.component';
import { GanttComponent } from './pages/gantt/gantt.component';
import { StatesComponent } from './pages/states/states.component';
import { CreateItemComponent } from './pages/create-item/create-item.component';
import { ItemDetailComponent } from './pages/item-detail/item-detail.component';
import { WorkflowsComponent } from './pages/workflows/workflows.component';
import { SprintsComponent } from './pages/sprints/sprints.component';

export const routes: Routes = [
  { path: '', pathMatch: 'full', redirectTo: 'board' },
  { path: 'setup', component: SetupComponent },
  { path: 'login', component: LoginComponent },
  { path: 'board', component: BoardComponent },
  { path: 'gantt', component: GanttComponent },
  { path: 'states', component: StatesComponent },
  { path: 'create', component: CreateItemComponent },
  { path: 'sprints', component: SprintsComponent },
  { path: 'workflows', component: WorkflowsComponent },
  { path: 'items/:id', component: ItemDetailComponent },
  { path: ':id', component: ItemDetailComponent },
  { path: '**', redirectTo: 'board' },
];
